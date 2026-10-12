/*
 * Allwinner's SD/MMC controller: the A20 and H3, the A64, and the H616 and
 * H313 - the last two being the reason this file exists, as the second
 * family of SoC this driver serves after the RK3566 of the target board.
 *
 * The part is a descendant of the Synopsys controller sdmmc_dwmshc.c drives,
 * and the lessons that file paid for on the board are applied here from the
 * start rather than learned again: the engine is taken out of the FIFO's way
 * for every programmed transfer, its status is enabled before it is waited
 * for, a write is over when the card lets go of DAT0, and nothing but a stop
 * or a status query goes to a busy card. What differs is layout: the divider
 * and the clock enable share one register, the FIFO moves to 0x200 from the
 * A31 on, and the A64 and later have a "new timing mode" that has to be
 * switched on and a sample delay that has to be put in software mode.
 *
 * What a variant changes is one row of the table below, picked by the
 * node's compatible strings; the clock, the bus gate and the reset line are
 * asked of sdmmc_clk.c by the names the binding gives them ("mmc", "ahb").
 *
 * Checked: on the controller QEMU emulates for the A64 (hw/sd/allwinner-
 * sdhost.c, the "bpim2u" machine), with an emulated card, by the bare-metal
 * harness in port/test/sunxi/ - which runs this file and the card layer
 * unchanged. Not checked: an H616 or H313 itself, which QEMU does not have
 * and which is not on the bench; in particular the H616's shifted descriptor
 * addresses and its clock unit are verified only by arithmetic.
 */

#include <minix/blockdriver.h>
#include <minix/cachectl.h>
#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/spin.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/mman.h>

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sdmmc.h"
#include "sdmmc_sunxi.h"

#define CMD_TIMEOUT_US		1000000
#define DATA_TIMEOUT_US		5000000
#define BUSY_TIMEOUT_US		10000000
#define RESET_TIMEOUT_US	100000
#define CLOCK_TIMEOUT_US	100000

/*
 * The variants, as Linux's sunxi-mmc.c sorts them. The first line whose
 * string the node carries wins, so the specific ones go first: an H616 node
 * says "allwinner,sun50i-h616-mmc", "allwinner,sun50i-a100-mmc".
 *
 *  shift	descriptor addresses in words rather than bytes (A100, H616)
 *  new_timing	the A64-and-later timing mode: NTSR bit 31
 *  mask_data0	hold DAT0 while the card clock changes (SD/SDIO ports)
 *  calibrate	put the sample delay line in software mode, delay zero -
 *		which is what Linux and Allwinner's own kernel do
 */
struct sx_variant {
	const char     *compatible;
	unsigned	shift;
	int		new_timing;
	int		mask_data0;
	int		calibrate;
	unsigned	fifo;
};

static const struct sx_variant variants[] = {
	{ "allwinner,sun50i-h616-mmc",	2, 1, 1, 1, SX_FIFO },
	{ "allwinner,sun50i-h616-emmc",	2, 1, 0, 1, SX_FIFO },
	{ "allwinner,sun50i-a100-mmc",	2, 1, 1, 1, SX_FIFO },
	{ "allwinner,sun50i-a100-emmc",	2, 1, 0, 1, SX_FIFO },
	{ "allwinner,sun50i-a64-mmc",	0, 1, 1, 1, SX_FIFO },
	{ "allwinner,sun50i-a64-emmc",	0, 1, 0, 1, SX_FIFO },
	{ "allwinner,sun7i-a20-mmc",	0, 0, 0, 0, SX_FIFO },
};

static phys_bytes reg_base;
static size_t reg_size;
static volatile uint8_t *regs;

static const struct sx_variant *variant;
static struct sdmmc_line clk_ahb, clk_mmc;
static unsigned rst_n;
static struct sdmmc_line rst[SDMMC_MAX_RESETS];
static uint32_t module_hz;		/* the module clock, as last set */
static int send_init;

static struct sdmmc_host *this_host;

static int irq_line = -1;
static int irq_hook;
static int irq_ok;
static int irq_dead;
#define IRQ_SPURIOUS_MAX	64

#define DESC_TABLE_BYTES	4096
#define NDESCS		(DESC_TABLE_BYTES / sizeof(struct sx_desc))

static int use_dma;
static struct sx_desc *desc;
static phys_bytes desc_phys;

/* The engine's completion for the transfer in flight; see sdmmc_dwmshc.c. */
static int dma_active;
static int dma_done;
static uint32_t dma_error;

static uint32_t
rd(unsigned off)
{
	return *(volatile uint32_t *)(regs + off);
}

static void
wr(unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(regs + off) = v;
}

static int
cache_op(int op, void *addr, size_t len)
{
	static int complained;
	int r;

	if ((r = sys_cachectl(op, addr, len)) != OK && !complained) {
		log_warn(&sdmmc_log, "cache maintenance refused (op %d): %d; "
		    "falling back to the programmed path\n", op, r);
		complained = 1;
	}
	return r;
}

static void
dump_regs_at(int level, const char *when)
{
	__log(&sdmmc_log, level, "%s: gctrl %08x clkcr %08x width %08x "
	    "cmd %08x ntsr %08x dmac %08x\n", when, rd(SX_GCTRL),
	    rd(SX_CLKCR), rd(SX_WIDTH), rd(SX_CMD), rd(SX_NTSR), rd(SX_DMAC));
	__log(&sdmmc_log, level, "%s: rint %08x imask %08x status %08x "
	    "idst %08x\n", when, rd(SX_RINT), rd(SX_IMASK), rd(SX_STATUS),
	    rd(SX_IDST));
}

static int
reset_gctrl(uint32_t bits)
{
	spin_t s;

	wr(SX_GCTRL, rd(SX_GCTRL) | bits);
	spin_init(&s, RESET_TIMEOUT_US);
	do {
		if ((rd(SX_GCTRL) & bits) == 0)
			return OK;
	} while (spin_check(&s));

	log_warn(&sdmmc_log, "reset 0x%x did not complete\n", bits);
	return EIO;
}

static int
check_intr(uint32_t wanted, uint32_t errors, uint32_t *got)
{
	uint32_t s = rd(SX_RINT), i;

	if (dma_active && (i = rd(SX_IDST)) != 0) {
		if (i & SX_IDST_ERROR)
			dma_error |= i;
		if (i & (SX_IDST_RX | SX_IDST_TX))
			dma_done = 1;
		wr(SX_IDST, i & SX_IDST_ALL);
	}

	if (got != NULL)
		*got = s;
	if (s & errors)
		return EIO;
	if (dma_error && (wanted & SX_INT_DTO))
		return EIO;
	if ((s & wanted) &&
	    (!(wanted & SX_INT_DTO) || !dma_active || dma_done))
		return OK;
	return EAGAIN;
}

static int
poll_intr(uint32_t wanted, uint32_t errors, uint32_t usecs, uint32_t *got)
{
	spin_t s;
	int r;

	spin_init(&s, usecs);
	do {
		if ((r = check_intr(wanted, errors, got)) != EAGAIN)
			return r;
	} while (spin_check(&s));

	return ETIMEDOUT;
}

/* The interrupt wait of sdmmc_sdhci.c and sdmmc_dwmshc.c, line for line. */
static int
intr_wait(uint32_t wanted, uint32_t errors, uint32_t usecs, uint32_t *got)
{
	message m;
	unsigned spurious = 0;
	int ipc_status, r, seen = 0, expired = 0;

	sys_setalarm(micros_to_ticks(usecs), 0);

	for (;;) {
		if ((r = check_intr(wanted, errors, got)) != EAGAIN)
			break;
		if (expired) {
			r = seen ? ETIMEDOUT : EAGAIN;
			if (r == EAGAIN)
				log_warn(&sdmmc_log, "no interrupt on line %d "
				    "in %u us; polling from now on\n",
				    irq_line, usecs);
			break;
		}
		if (spurious > IRQ_SPURIOUS_MAX) {
			log_warn(&sdmmc_log, "line %d interrupted %u times "
			    "with nothing to show for it; polling from now "
			    "on\n", irq_line, spurious);
			r = EAGAIN;
			break;
		}
		if (sys_irqenable(&irq_hook) != OK) {
			r = EAGAIN;
			break;
		}
		if (driver_receive(ANY, &m, &ipc_status) != OK) {
			r = EIO;
			break;
		}
		if (is_ipc_notify(ipc_status)) {
			if (_ENDPOINT_P(m.m_source) == CLOCK) {
				expired = 1;
				continue;
			}
			if (_ENDPOINT_P(m.m_source) == HARDWARE) {
				seen = 1;
				spurious++;
				continue;
			}
		}
		blockdriver_mq_queue(&m, ipc_status);
	}

	sys_setalarm(0, 0);
	(void)sys_irqdisable(&irq_hook);

	if (r == EAGAIN)
		irq_dead = 1;
	return r;
}

static int
wait_intr(uint32_t wanted, uint32_t errors, uint32_t usecs, uint32_t *got)
{
	int r;

	if (irq_ok && !irq_dead) {
		if ((r = intr_wait(wanted, errors, usecs, got)) != EAGAIN)
			return r;
	}
	return poll_intr(wanted, errors, usecs, got);
}

static int
wait_cmd_taken(uint32_t usecs)
{
	spin_t s;

	spin_init(&s, usecs);
	do {
		if ((rd(SX_CMD) & SX_CMD_START) == 0)
			return OK;
	} while (spin_check(&s));
	return ETIMEDOUT;
}

static int
wait_not_busy(uint32_t usecs)
{
	spin_t s;

	spin_init(&s, usecs);
	do {
		if ((rd(SX_STATUS) & SX_STATUS_DATA_BUSY) == 0)
			return OK;
	} while (spin_check(&s));
	return ETIMEDOUT;
}

/*
 * Make the card interface take CLKCR - a command to the part, not the card.
 * With mask_data0 the part is told to ignore DAT0 meanwhile, which Linux
 * does on every port that can see a card hold it.
 */
static int
update_clock(void)
{
	int r;

	if (variant->mask_data0)
		wr(SX_CLKCR, rd(SX_CLKCR) | SX_CLKCR_MASK_DATA0);
	wr(SX_CMD, SX_CMD_START | SX_CMD_UPD_CLK | SX_CMD_PRV_DAT_WAIT);
	r = wait_cmd_taken(CLOCK_TIMEOUT_US);
	wr(SX_RINT, rd(SX_RINT) & ~(1u << 16));
	if (variant->mask_data0)
		wr(SX_CLKCR, rd(SX_CLKCR) & ~SX_CLKCR_MASK_DATA0);
	if (r != OK) {
		log_warn(&sdmmc_log, "the card interface would not take a "
		    "clock update\n");
		dump_regs_at(LEVEL_WARN, "clock update");
	}
	return r;
}

/*
 * The card clock: the module clock from the clock unit, divided by 2N in the
 * part when N is not zero. The module clock is asked for the card clock
 * itself, as Linux does, so N is zero whenever the unit is one this driver
 * knows; the divider is there for a machine whose unit it does not, where the
 * module rate is taken to be the tree's max-frequency - an over-estimate if
 * anything, which makes the card slower, never faster.
 */
static int
sx_set_clock(uint32_t hz, uint32_t *actual)
{
	uint32_t v, n, got, src;
	int r;

	wr(SX_CLKCR, rd(SX_CLKCR) & ~(SX_CLKCR_ON | SX_CLKCR_LOW_POWER));
	if ((r = update_clock()) != OK)
		return r;

	if (hz == 0) {
		if (actual != NULL)
			*actual = 0;
		return OK;
	}

	if (sdmmc_clk_set_rate(&clk_mmc, hz, &src) == OK)
		module_hz = src;

	if (module_hz <= hz) {
		n = 0;
		got = module_hz;
	} else {
		n = (module_hz + 2 * hz - 1) / (2 * hz);
		if (n > SX_CLKCR_DIV_MASK)
			n = SX_CLKCR_DIV_MASK;
		got = module_hz / (2 * n);
	}

	v = rd(SX_CLKCR) & ~SX_CLKCR_DIV_MASK;
	wr(SX_CLKCR, v | n);
	if (variant->new_timing)
		wr(SX_NTSR, rd(SX_NTSR) | SX_NTSR_NEW_MODE);
	if (variant->calibrate)
		wr(SX_SAMP_DL, SX_SAMP_DL_SW_EN);
	if ((r = update_clock()) != OK)
		return r;

	wr(SX_CLKCR, rd(SX_CLKCR) | SX_CLKCR_ON);
	if ((r = update_clock()) != OK)
		return r;

	if (actual != NULL)
		*actual = got;
	log_debug(&sdmmc_log, "clock %u Hz asked, %u Hz set (module %u, "
	    "N %u)\n", hz, got, module_hz, n);
	return OK;
}

static int
sx_set_bus_width(unsigned bits)
{
	switch (bits) {
	case 1: wr(SX_WIDTH, SX_WIDTH_1); return OK;
	case 4: wr(SX_WIDTH, SX_WIDTH_4); return OK;
	case 8: wr(SX_WIDTH, SX_WIDTH_8); return OK;
	}
	return EINVAL;
}

/* SDR high speed differs only in rate on this side of the bus. */
static int
sx_set_timing(int UNUSED(high_speed))
{
	wr(SX_GCTRL, rd(SX_GCTRL) & ~SX_GCTRL_DDR);
	return OK;
}

static void
dma_stop(void)
{
	wr(SX_DMAC, 0);
	wr(SX_GCTRL, rd(SX_GCTRL) & ~SX_GCTRL_DMA_ENABLE);
	dma_active = 0;
}

static void
recover(void)
{
	dma_stop();
	(void)reset_gctrl(SX_GCTRL_FIFO_RESET | SX_GCTRL_DMA_RESET);
	wr(SX_IDST, SX_IDST_ALL);
	wr(SX_RINT, SX_INT_ALL);
	(void)update_clock();
}

static int
command_error(struct sdmmc_cmd *cmd, uint32_t st)
{
	recover();

	/*
	 * No answer, which is how the card layer learns what kind of card it
	 * has. A real part says so with the timeout bit; QEMU's model says it
	 * with the response-error bit alone, and neither is worth a warning.
	 */
	if ((st & (SX_INT_RTO | SX_INT_RE)) &&
	    !(st & (SX_INT_RCRC | SX_INT_DATA_ERROR))) {
		log_debug(&sdmmc_log, "CMD%u arg %08x: no answer (rint %08x)\n",
		    cmd->index, cmd->arg, st);
		return ETIMEDOUT;
	}

	log_warn(&sdmmc_log, "CMD%u: error status 0x%08x\n", cmd->index, st);
	return EIO;
}

static int
dma_setup(struct sdmmc_cmd *cmd)
{
	size_t total = (size_t)cmd->blocks * cmd->blocklen;
	phys_bytes addr = cmd->data_phys;
	size_t left = total;
	unsigned n = 0, sh = variant->shift;
	int op;

	if (!use_dma || cmd->data_phys == 0 || total == 0)
		return EINVAL;
	if ((addr & 3) != 0 || ((uint64_t)addr + total) >> sh > 0x100000000ULL)
		return EINVAL;
	if ((total + SX_DESC_MAX_LEN - 1) / SX_DESC_MAX_LEN > NDESCS)
		return EINVAL;

	while (left > 0) {
		size_t len = (left > SX_DESC_MAX_LEN) ? SX_DESC_MAX_LEN : left;
		uint32_t config = SX_DES_OWN | SX_DES_CH;

		if (n == 0)
			config |= SX_DES_FD;
		if (len == left)
			config |= SX_DES_LD | SX_DES_ER;
		else
			config |= SX_DES_DIC;
		desc[n].config = config;
		desc[n].size = (uint32_t)len;
		desc[n].buf = (uint32_t)(addr >> sh);
		desc[n].next = (len == left) ? 0 : (uint32_t)((desc_phys +
		    (n + 1) * sizeof(struct sx_desc)) >> sh);
		addr += len;
		left -= len;
		n++;
	}

	op = (cmd->data_dir == SDMMC_DATA_WRITE) ?
	    CACHE_CLEAN : CACHE_INVALIDATE;
	if (cache_op(op, cmd->data, total) != OK)
		return EIO;
	if (cache_op(CACHE_CLEAN, desc, n * sizeof(struct sx_desc)) != OK)
		return EIO;

	/* NetBSD's and Linux's order: list, FIFO levels, interface, engine. */
	wr(SX_DLBA, (uint32_t)(desc_phys >> sh));
	wr(SX_FTRGL, SX_FTRGL_DEFAULT);
	wr(SX_GCTRL, (rd(SX_GCTRL) & ~SX_GCTRL_AHB) | SX_GCTRL_DMA_ENABLE);
	if (reset_gctrl(SX_GCTRL_DMA_RESET) != OK)
		return EIO;
	wr(SX_DMAC, SX_DMAC_SOFT_RESET);
	wr(SX_IDST, SX_IDST_ALL);
	dma_active = 1;
	dma_done = 0;
	dma_error = 0;
	wr(SX_DMAC, SX_DMAC_IDMA_ON | SX_DMAC_FIX_BURST);
	return OK;
}

/* The programmed path: the FIFO by the processor, word by word. */
static int
pio_transfer(struct sdmmc_cmd *cmd)
{
	uint32_t *p = cmd->data;
	uint32_t left = (cmd->blocks * cmd->blocklen) / 4;
	spin_t s;

	spin_init(&s, DATA_TIMEOUT_US);
	while (left > 0) {
		uint32_t st;

		if (rd(SX_RINT) & SX_INT_DATA_ERROR)
			return EIO;
		st = rd(SX_STATUS);
		if (cmd->data_dir == SDMMC_DATA_READ) {
			if ((st & SX_STATUS_FIFO_EMPTY) == 0) {
				*p++ = rd(variant->fifo);
				left--;
				continue;
			}
		} else if ((st & SX_STATUS_FIFO_FULL) == 0) {
			wr(variant->fifo, *p++);
			left--;
			continue;
		}
		if (!spin_check(&s))
			return ETIMEDOUT;
	}
	wr(SX_RINT, SX_INT_RXDR | SX_INT_TXDR);
	return OK;
}

static int
sx_command(struct sdmmc_cmd *cmd)
{
	uint32_t cmdreg, st, i;
	int r, dma = 0, has_data;

	if (regs == NULL)
		return ENXIO;
	has_data = (cmd->data_dir != SDMMC_DATA_NONE);

	if ((r = wait_cmd_taken(CMD_TIMEOUT_US)) != OK) {
		dump_regs_at(LEVEL_WARN, "stuck");
		return r;
	}
	/* A busy card takes a stop or a status query and nothing else. */
	if (cmd->index != 12 && cmd->index != 13 &&
	    wait_not_busy(BUSY_TIMEOUT_US) != OK) {
		log_warn(&sdmmc_log, "CMD%u: the card is still busy\n",
		    cmd->index);
		return EBUSY;
	}

	wr(SX_RINT, SX_INT_ALL);

	cmdreg = SX_CMD_START | SX_CMD_INDEX(cmd->index);
	if (send_init) {
		cmdreg |= SX_CMD_INIT;
		send_init = 0;
	}
	if (cmd->index == 12)
		cmdreg |= SX_CMD_STOP_ABORT;

	switch (cmd->rsp_type) {
	case SDMMC_RSP_NONE:
		break;
	case SDMMC_RSP_R2:
		cmdreg |= SX_CMD_RESP_EXP | SX_CMD_RESP_LONG | SX_CMD_RESP_CRC;
		break;
	case SDMMC_RSP_R3:
		cmdreg |= SX_CMD_RESP_EXP;
		break;
	default:
		cmdreg |= SX_CMD_RESP_EXP | SX_CMD_RESP_CRC;
		break;
	}

	if (has_data) {
		if (cmd->blocklen % 4 != 0 || ((uintptr_t)cmd->data & 3) != 0)
			return EINVAL;
		if ((r = reset_gctrl(SX_GCTRL_FIFO_RESET)) != OK)
			return r;
		wr(SX_BLKSZ, cmd->blocklen);
		wr(SX_BYTECNT, cmd->blocks * cmd->blocklen);

		cmdreg |= SX_CMD_DATA_EXP | SX_CMD_PRV_DAT_WAIT;
		if (cmd->data_dir == SDMMC_DATA_WRITE)
			cmdreg |= SX_CMD_WRITE;
		if (cmd->stop && cmd->blocks > 1)
			cmdreg |= SX_CMD_SEND_STOP;
		wr(SX_A12A, (cmdreg & SX_CMD_SEND_STOP) ? 0 : 0xffff);

		if (dma_setup(cmd) == OK) {
			dma = 1;
		} else {
			/* The processor's: engine out, FIFO to the bus. */
			dma_stop();
			wr(SX_GCTRL, rd(SX_GCTRL) | SX_GCTRL_AHB);
		}
	}

	wr(SX_ARG, cmd->arg);
	log_debug(&sdmmc_log, "CMD%u: cmd %08x arg %08x%s\n", cmd->index,
	    cmdreg, cmd->arg, dma ? " dma" : "");
	wr(SX_CMD, cmdreg);

	r = wait_intr(SX_INT_CMD_DONE, SX_INT_CMD_ERROR, CMD_TIMEOUT_US, &st);
	if (r == OK && (st & SX_INT_CMD_ERROR))
		r = EIO;
	if (r != OK) {
		if (r == EIO)
			return command_error(cmd, st);
		log_warn(&sdmmc_log, "CMD%u: no completion\n", cmd->index);
		dump_regs_at(LEVEL_WARN, "no completion");
		recover();
		return r;
	}
	wr(SX_RINT, SX_INT_CMD_DONE);

	if (cmd->rsp_type == SDMMC_RSP_R2) {
		/* Whole, CRC byte and all, as on the DesignWare part. */
		for (i = 0; i < 4; i++)
			cmd->resp[i] = rd(SX_RESP0 + 4 * i);
		log_debug(&sdmmc_log, "CMD%u: raw %08x %08x %08x %08x\n",
		    cmd->index, cmd->resp[3], cmd->resp[2], cmd->resp[1],
		    cmd->resp[0]);
		cmd->resp[0] &= ~0xffu;
	} else if (cmd->rsp_type != SDMMC_RSP_NONE) {
		cmd->resp[0] = rd(SX_RESP0);
		log_debug(&sdmmc_log, "CMD%u: raw %08x\n", cmd->index,
		    cmd->resp[0]);
	}

	if (has_data) {
		if (!dma && (r = pio_transfer(cmd)) != OK) {
			st = rd(SX_RINT);
			log_warn(&sdmmc_log, "CMD%u: programmed transfer "
			    "failed (%d)\n", cmd->index, r);
			return (r == EIO) ? command_error(cmd, st) :
			    (recover(), r);
		}

		r = wait_intr(SX_INT_DTO, SX_INT_DATA_ERROR, DATA_TIMEOUT_US,
		    &st);
		if (r == EIO)
			return command_error(cmd, st);
		if (r != OK) {
			log_warn(&sdmmc_log, "CMD%u: transfer never completed "
			    "(rint %08x, engine %s)\n", cmd->index, st,
			    !dma ? "not used" : dma_done ? "done" : "silent");
			dump_regs_at(LEVEL_WARN, "transfer");
			recover();
			return r;
		}
		if (dma) {
			uint32_t e = dma_error;

			dma_stop();
			if (e != 0) {
				log_warn(&sdmmc_log, "CMD%u: the DMA engine "
				    "reports 0x%08x\n", cmd->index, e);
				return command_error(cmd, st);
			}
		}
		wr(SX_RINT, SX_INT_DTO);

		if (cmdreg & SX_CMD_SEND_STOP) {
			if (poll_intr(SX_INT_ACD, 0, CMD_TIMEOUT_US, &st) != OK)
				log_warn(&sdmmc_log, "CMD%u: the automatic "
				    "stop never completed\n", cmd->index);
			wr(SX_RINT, SX_INT_ACD);
		}
		if (cmd->data_dir == SDMMC_DATA_WRITE &&
		    wait_not_busy(BUSY_TIMEOUT_US) != OK) {
			log_warn(&sdmmc_log, "CMD%u: the card stayed busy after "
			    "the write\n", cmd->index);
			return ETIMEDOUT;
		}
		if (dma && cmd->data_dir == SDMMC_DATA_READ)
			(void)cache_op(CACHE_INVALIDATE, cmd->data,
			    (size_t)cmd->blocks * cmd->blocklen);
	} else if (cmd->rsp_type == SDMMC_RSP_R1B) {
		if (wait_not_busy(BUSY_TIMEOUT_US) != OK) {
			log_warn(&sdmmc_log, "CMD%u: the card stayed busy\n",
			    cmd->index);
			return ETIMEDOUT;
		}
	}

	return OK;
}

static int
sx_init(void)
{
	void *v;
	int r;

	/* The bus clock and reset before the first register access. */
	(void)sdmmc_clk_enable(&clk_ahb);
	(void)sdmmc_clk_enable(&clk_mmc);
	sdmmc_reset_pulse(rst, rst_n);

	if (regs == NULL) {
		v = vm_map_phys(SELF, (void *)reg_base, reg_size);
		if (v == MAP_FAILED) {
			log_warn(&sdmmc_log, "cannot map registers at 0x%lx; "
			    "not granted by RS?\n", (unsigned long)reg_base);
			return EPERM;
		}
		regs = v;
	}

	dump_regs_at(LEVEL_INFO, "as found");

	/*
	 * GCTRL whole, then reset: the reset bits clear themselves and leave
	 * the rest, and a bootloader that read the card by DMA leaves the
	 * engine selected - the lesson of sdmmc_dwmshc.c on the board.
	 */
	wr(SX_DMAC, SX_DMAC_SOFT_RESET);
	wr(SX_GCTRL, 0);
	if ((r = reset_gctrl(SX_GCTRL_ALL_RESET)) != OK)
		return r;
	wr(SX_DMAC, 0);
	wr(SX_TMOUT, 0xffffffffu);
	wr(SX_IMASK, 0);
	wr(SX_RINT, SX_INT_ALL);
	wr(SX_IDST, SX_IDST_ALL);
	wr(SX_IDIE, SX_IDST_TX | SX_IDST_RX | SX_IDST_ERROR | SX_IDST_NIS);
	wr(SX_FTRGL, SX_FTRGL_DEFAULT);

	if (desc == NULL)
		desc = alloc_contig(DESC_TABLE_BYTES, AC_ALIGN4K, &desc_phys);
	use_dma = (desc != NULL && desc_phys != 0 &&
	    (desc_phys + DESC_TABLE_BYTES) >> variant->shift <= 0xffffffffULL);
	if (!use_dma)
		log_warn(&sdmmc_log, "no descriptor table the engine can "
		    "reach; the processor will move the data\n");

	if (this_host != NULL) {
		this_host->max_blocks_pio = 8;
		this_host->max_blocks = use_dma ?
		    (unsigned)(NDESCS * SX_DESC_MAX_LEN / SDMMC_SECTOR_SIZE) :
		    this_host->max_blocks_pio;
	}

	if (irq_line >= 0 && !irq_ok) {
		irq_hook = SDMMC_IRQ_HOOK;
		if ((r = sys_irqsetpolicy(irq_line, 0, &irq_hook)) != OK)
			log_warn(&sdmmc_log, "line %d not granted (%d); every "
			    "wait will be a poll\n", irq_line, r);
		else
			irq_ok = 1;
	}
	if (irq_ok) {
		wr(SX_IMASK, SX_INT_CMD_DONE | SX_INT_DTO |
		    SX_INT_CMD_ERROR | SX_INT_DATA_ERROR);
		wr(SX_GCTRL, rd(SX_GCTRL) | SX_GCTRL_INT_ENABLE);
	}

	send_init = 1;
	(void)sx_set_bus_width(1);
	(void)sx_set_timing(0);
	if ((r = sx_set_clock(SDMMC_IDENT_HZ, NULL)) != OK)
		return r;

	log_info(&sdmmc_log, "Allwinner MMC (%s) at 0x%lx, transfers by %s, "
	    "waits by %s\n", variant->compatible, (unsigned long)reg_base,
	    use_dma ? "internal DMA" : "the processor",
	    irq_ok ? "interrupt" : "polling");
	return OK;
}

static void
sx_exit(void)
{
	if (regs != NULL) {
		wr(SX_IMASK, 0);
		wr(SX_GCTRL, rd(SX_GCTRL) & ~SX_GCTRL_INT_ENABLE);
		(void)sx_set_clock(0, NULL);
		vm_unmap_phys(SELF, (void *)regs, reg_size);
	}
	if (irq_ok) {
		(void)sys_irqrmpolicy(&irq_hook);
		irq_ok = 0;
	}
	regs = NULL;
}

int
sunxi_probe(const struct fdt_node *node, const struct sdmmc_devinfo *info,
	struct sdmmc_host *host)
{
	unsigned i;

	if (info->size < 0x200 + 4)
		return ENXIO;

	variant = NULL;
	for (i = 0; i < sizeof(variants) / sizeof(variants[0]); i++) {
		if (fdt_node_is_compatible(node, variants[i].compatible)) {
			variant = &variants[i];
			break;
		}
	}
	if (variant == NULL)
		return ENXIO;

	reg_base = info->base;
	reg_size = info->size;
	irq_line = info->irq;
	this_host = host;

	rst_n = info->nresets;
	if (rst_n > SDMMC_MAX_RESETS)
		rst_n = SDMMC_MAX_RESETS;
	memcpy(rst, info->reset, sizeof(rst));

	/* "ahb" is the bus gate, "mmc" the module clock, in every binding. */
	if (sdmmc_clk_get(node, "ahb", &clk_ahb) != OK)
		clk_ahb.ctl = SDMMC_NO_LINE;
	if (sdmmc_clk_get(node, "mmc", &clk_mmc) != OK)
		clk_mmc.ctl = SDMMC_NO_LINE;

	module_hz = (info->max_freq != 0) ? info->max_freq : 50000000;

	host->init = sx_init;
	host->exit = sx_exit;
	host->set_clock = sx_set_clock;
	host->set_bus_width = sx_set_bus_width;
	host->set_timing = sx_set_timing;
	host->command = sx_command;
	host->max_bus_width = info->bus_width;
	host->max_freq = info->max_freq;
	host->max_blocks = 8;
	host->max_blocks_pio = 8;
	return OK;
}
