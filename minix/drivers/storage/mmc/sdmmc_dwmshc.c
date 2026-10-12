/*
 * The Synopsys DesignWare Mobile Storage Host Controller: the SD socket of
 * the RK3566, and the same block on Exynos, SoCFPGA, HiSilicon and StarFive.
 *
 * The node is "rockchip,rk3568-dw-mshc" / "rockchip,rk3288-dw-mshc" on the
 * target board, "snps,dw-mshc" in the generic binding. Nothing in this file
 * is a Rockchip register: the clock that feeds the part and the phase of its
 * sample and drive clocks are asked of sdmmc_clk.c by the names the tree
 * gives them, and what differs between SoCs at the level of this part - the
 * Rockchip glue halves the clock before the part's own divider - is one row
 * of the table below.
 *
 * Three things about this part shape the file, and none of them is in the SD
 * standard:
 *
 *  - The clock is not changed by writing the divider. CLKDIV, CLKSRC and
 *    CLKENA are latched by the card interface only when a command with
 *    UPD_CLK is sent through the command register and accepted; until then
 *    the old clock runs. update_clock() is that command.
 *
 *  - A 136-bit response arrives whole, CRC byte included - unlike an SD Host
 *    Controller, which drops it and shifts the rest down by eight. So the
 *    response registers are already numbered the way the specification
 *    numbers the CSD and the CID, and only the CRC byte is cleared, to match
 *    what the other host hands the card layer.
 *
 *  - It has its own DMA engine (IDMAC) with a descriptor format of its own.
 *    Used when the part was built with it and with 32-bit addresses (HCON
 *    says which); the processor moves the data otherwise, through a FIFO that
 *    stops the card clock when it is full or empty, so the programmed path
 *    needs no ceiling of the kind the SDHCI part on the same board has.
 *
 * The reference is NetBSD's dwc_mmc.c and dwcmmc_fdt.c, read beside the
 * registers of the vendor system on the board (port/cb2-sd/linux-reference
 * .txt): the command word that system last issued, 0x20002352, is exactly
 * what command() below builds for a multi-block read.
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
#include "sdmmc_dwmshc.h"

/* Deadlines, as sdmmc_sdhci.c has them and for the same reasons. */
#define CMD_TIMEOUT_US		1000000
#define DATA_TIMEOUT_US		5000000
#define BUSY_TIMEOUT_US		10000000
#define RESET_TIMEOUT_US	100000
#define CLOCK_TIMEOUT_US	100000

/*
 * What differs between the SoCs this part lives on, as far as this file is
 * concerned. The first match on the node's compatible list wins, so the more
 * specific line goes first.
 *
 * ciu_div: Rockchip feeds the part through a fixed divide-by-two (Linux's
 * RK3288_CLKGEN_DIV, NetBSD's ciu_div), so the clock to ask the clock
 * controller for is twice the rate the part's own divider starts from.
 *
 * phases: the Rockchip binding names a drive and a sample clock whose phase
 * against the card clock is set in the clock controller. Linux drives at 90
 * degrees in every mode up to SD high speed and samples at the board's
 * "rockchip,default-sample-phase", zero when it gives none; so does this.
 */
struct dw_variant {
	const char     *compatible;
	unsigned	ciu_div;
	int		phases;
};

static const struct dw_variant variants[] = {
	{ "rockchip,rk3288-dw-mshc",	2, 1 },
	{ "snps,dw-mshc",		1, 0 },
};

#define DRIVE_PHASE		90

/* Where the registers are, and where they ended up. */
static phys_bytes reg_base;
static size_t reg_size;
static volatile uint8_t *regs;

static const struct dw_variant *variant;
static struct sdmmc_line clk_biu, clk_ciu, clk_drive, clk_sample;
static unsigned rst_n;
static struct sdmmc_line rst[SDMMC_MAX_RESETS];
static unsigned sample_phase;

static uint32_t fifo_depth;		/* in 32-bit words */
static unsigned data_reg;		/* DW_DATA_OLD or DW_DATA_240A */
static uint32_t ciu_hz;			/* what the part's input runs at */
static int send_init;			/* 80 clocks before the first command */
static int non_removable, broken_cd, gpio_cd;

static struct sdmmc_host *this_host;

/* The interrupt: see sdmmc_sdhci.c, which this follows line for line. */
static int irq_line = -1;
static int irq_hook;
static int irq_ok;
static int irq_dead;
#define IRQ_SPURIOUS_MAX	64

/*
 * The internal DMA engine's descriptors: one page, so 256 of them, each up
 * to DW_DESC_MAX_LEN - a megabyte per command, thirty-two times what the
 * block layer ever asks for.
 */
#define IDMAC_TABLE_BYTES	4096
#define IDMAC_DESCS	(IDMAC_TABLE_BYTES / sizeof(struct dw_idmac_desc))

static int use_idmac;
static struct dw_idmac_desc *idmac_desc;
static phys_bytes idmac_desc_phys;

/*
 * The engine's own completion, for the transfer in flight.
 *
 * The part reports "the card is done" (DTO in RINTSTS) and "the engine is
 * done" (RI or TI in IDSTS) separately, and a read is over only when both
 * are: the last words can still be on their way from the FIFO to memory
 * when the card has finished. check_intr() therefore watches IDSTS while
 * a transfer by the engine is running, acknowledges what it sees there -
 * the summary bit keeps the interrupt line up until it is cleared - and
 * remembers it.
 *
 * IDSTS reports nothing at all unless IDINTEN enables it: the first version
 * of this file left IDINTEN at zero, the vendor system and NetBSD both set
 * it, and on the board every read ended with the card done, the data moved,
 * and IDSTS still reading zero.
 */
static int idma_active;
static int idma_done;
static uint32_t idma_error;

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

/* Cache maintenance with the answer looked at; see sdmmc_sdhci.c. */
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

/*
 * The registers worth seeing, in two short lines: the board's console drops
 * the middle of long ones, and a register dump with a hole in it reads as a
 * number. At debug level when things are going well; at warning level from
 * the failure paths, because a failure that does not say what the part
 * looked like has to be reproduced before it can be read.
 */
static void
dump_regs_at(int level, const char *when)
{
	__log(&sdmmc_log, level, "%s: ctrl %08x clkdiv %08x clkena %08x "
	    "ctype %08x cmd %08x bmod %08x\n", when, rd(DW_CTRL), rd(DW_CLKDIV),
	    rd(DW_CLKENA), rd(DW_CTYPE), rd(DW_CMD), rd(DW_BMOD));
	__log(&sdmmc_log, level, "%s: rint %08x mask %08x status %08x "
	    "idsts %08x cdet %08x uhs %08x\n", when, rd(DW_RINTSTS),
	    rd(DW_INTMASK), rd(DW_STATUS), rd(DW_IDSTS), rd(DW_CDETECT),
	    rd(DW_UHS_REG));
}

static void
dump_regs(const char *when)
{
	dump_regs_at(LEVEL_DEBUG, when);
}

/* Set some of the CTRL reset bits and wait for the part to clear them. */
static int
reset_ctrl(uint32_t bits)
{
	spin_t s;

	wr(DW_CTRL, rd(DW_CTRL) | bits);
	spin_init(&s, RESET_TIMEOUT_US);
	do {
		if ((rd(DW_CTRL) & bits) == 0)
			return OK;
	} while (spin_check(&s));

	log_warn(&sdmmc_log, "reset 0x%x did not complete (ctrl %08x)\n",
	    bits, rd(DW_CTRL));
	return EIO;
}

/*
 * Has one of the wanted interrupt bits arrived, or an error? Errors first,
 * for the reason sdmmc_sdhci.c gives: a transfer that ends badly can raise
 * the bit being waited for in the same word.
 */
static int
check_intr(uint32_t wanted, uint32_t errors, uint32_t *got)
{
	uint32_t s = rd(DW_RINTSTS), i;

	if (idma_active && (i = rd(DW_IDSTS)) != 0) {
		if (i & DW_IDSTS_ERROR)
			idma_error |= i;
		if (i & (DW_IDSTS_RI | DW_IDSTS_TI))
			idma_done = 1;
		wr(DW_IDSTS, i);
	}

	if (got != NULL)
		*got = s;
	if (s & errors)
		return EIO;
	if (idma_error && (wanted & DW_INT_DTO))
		return EIO;
	if ((s & wanted) &&
	    (!(wanted & DW_INT_DTO) || !idma_active || idma_done))
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

/*
 * Sleep until one of them arrives, with the three ways for the line to prove
 * useless that sdmmc_sdhci.c checks for. EAGAIN means "poll from now on".
 */
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
			    "with nothing to show for it (rint %08x mask %08x); "
			    "polling from now on\n", irq_line, spurious,
			    rd(DW_RINTSTS), rd(DW_INTMASK));
			r = EAGAIN;
			break;
		}

		if (sys_irqenable(&irq_hook) != OK) {
			log_warn(&sdmmc_log, "cannot enable line %d; polling "
			    "from now on\n", irq_line);
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

/* Wait until the part has taken the last word written to DW_CMD. */
static int
wait_cmd_taken(uint32_t usecs)
{
	spin_t s;

	spin_init(&s, usecs);
	do {
		if ((rd(DW_CMD) & DW_CMD_START) == 0)
			return OK;
	} while (spin_check(&s));
	return ETIMEDOUT;
}

/*
 * Tell the card interface to pick up CLKDIV, CLKSRC and CLKENA.
 *
 * Nothing goes out on the bus: UPD_CLK is a command to the part, not to the
 * card. A hardware-locked error means the interface was still busy with the
 * last one, and the databook's answer is to send it again.
 */
static int
update_clock(void)
{
	uint32_t cmd;
	unsigned tries;

	cmd = DW_CMD_START | DW_CMD_UPD_CLK | DW_CMD_PRV_DAT_WAIT |
	    DW_CMD_USE_HOLD_REG;

	for (tries = 0; tries < 10; tries++) {
		wr(DW_RINTSTS, DW_INT_HLE);
		wr(DW_CMDARG, 0);
		wr(DW_CMD, cmd);
		if (wait_cmd_taken(CLOCK_TIMEOUT_US) != OK)
			break;
		if ((rd(DW_RINTSTS) & DW_INT_HLE) == 0)
			return OK;
	}

	log_warn(&sdmmc_log, "the card interface would not take a clock "
	    "update\n");
	dump_regs_at(LEVEL_WARN, "clock update");
	return EIO;
}

/*
 * The card clock.
 *
 * Its source is the part's input divided by ciu_div, and the part divides
 * that again by 2 * CLKDIV, or not at all when CLKDIV is zero. So the input
 * is asked for ciu_div times the wish - which is what Linux and NetBSD both
 * do - and the divider takes whatever the clock controller could not.
 *
 * Should the controller not be one sdmmc_clk.c knows, the input stays where
 * the firmware left it, and that rate is unknown. The tree's max-frequency is
 * then taken as the input, which can only make the computed divider too
 * large: a card that runs slower than asked, never faster.
 */
static int
dw_set_clock(uint32_t hz, uint32_t *actual)
{
	uint32_t src, bus, n, got;
	int r;

	wr(DW_CLKENA, 0);
	wr(DW_CLKSRC, 0);
	if ((r = update_clock()) != OK)
		return r;

	if (hz == 0) {
		if (actual != NULL)
			*actual = 0;
		return OK;
	}

	if (sdmmc_clk_set_rate(&clk_ciu, hz * variant->ciu_div, &src) == OK)
		ciu_hz = src;
	bus = ciu_hz / variant->ciu_div;

	if (bus <= hz) {
		n = 0;
		got = bus;
	} else {
		n = (bus + 2 * hz - 1) / (2 * hz);
		if (n > 0xff)
			n = 0xff;
		got = bus / (2 * n);
	}

	wr(DW_CLKDIV, n);
	if ((r = update_clock()) != OK)
		return r;

	/*
	 * Low power: the part stops the card clock while the bus is idle.
	 * Linux does this for every card that is not SDIO - an SDIO card
	 * signals interrupts on DAT1 and needs the clock to do it - and the
	 * vendor system on the board runs the socket that way (CLKENA
	 * 0x00010001).
	 */
	wr(DW_CLKENA, DW_CLKENA_ON | DW_CLKENA_LOW_POWER);
	if ((r = update_clock()) != OK)
		return r;

	if (variant->phases) {
		(void)sdmmc_clk_set_phase(&clk_drive, DRIVE_PHASE);
		(void)sdmmc_clk_set_phase(&clk_sample, sample_phase);
	}

	if (actual != NULL)
		*actual = got;
	log_debug(&sdmmc_log, "clock %u Hz asked, %u Hz set (input %u, "
	    "CLKDIV %u)\n", hz, got, ciu_hz, n);
	return OK;
}

static int
dw_set_bus_width(unsigned bits)
{
	switch (bits) {
	case 1:
		wr(DW_CTYPE, DW_CTYPE_1BIT);
		return OK;
	case 4:
		wr(DW_CTYPE, DW_CTYPE_4BIT);
		return OK;
	case 8:
		wr(DW_CTYPE, DW_CTYPE_8BIT);
		return OK;
	}
	return EINVAL;
}

/*
 * High speed has no bit here: the part samples on the edge its sample clock
 * says, and SD high speed and the default mode differ only in rate on this
 * side of the bus. DDR, which does have a bit, is not used.
 */
static int
dw_set_timing(int UNUSED(high_speed))
{
	wr(DW_UHS_REG, rd(DW_UHS_REG) & ~DW_UHS_DDR);
	return OK;
}

/*
 * After anything has gone wrong: empty the FIFO, stop the engine, and clear
 * every status bit, so that the next command starts from nothing. The
 * databook's recovery is the FIFO and DMA resets; the controller reset is
 * kept for init, because it also forgets the clock.
 */
static void
recover(void)
{
	wr(DW_BMOD, rd(DW_BMOD) & ~DW_BMOD_DE);
	wr(DW_CTRL, rd(DW_CTRL) & ~DW_CTRL_USE_IDMAC);
	(void)reset_ctrl(DW_CTRL_FIFO_RESET | DW_CTRL_DMA_RESET);
	wr(DW_IDSTS, 0xffffffffu);
	wr(DW_RINTSTS, DW_INT_ALL);
}

static int
command_error(struct sdmmc_cmd *cmd, uint32_t st)
{
	uint32_t idsts = rd(DW_IDSTS);

	recover();

	if ((st & DW_INT_RTO) && !(st & (DW_INT_RCRC | DW_INT_RE))) {
		/* How the card layer learns what kind of card it has; not a
		 * complaint, as in sdmmc_sdhci.c. */
		log_debug(&sdmmc_log, "CMD%u arg %08x: no answer (rint %08x)\n",
		    cmd->index, cmd->arg, st);
		return ETIMEDOUT;
	}

	log_warn(&sdmmc_log, "CMD%u: error status 0x%08x, dma 0x%08x\n",
	    cmd->index, st, idsts);
	return EIO;
}

/*
 * Hand the buffer to the engine: one descriptor per 4 KiB, chained, the last
 * one marked. Refuses - and the caller goes through the FIFO - for exactly
 * the cases sdmmc_sdhci.c's dma_setup() refuses: no physical address, not
 * below 4 GiB, not word aligned, too long for the table.
 */
static int
dma_setup(struct sdmmc_cmd *cmd)
{
	size_t total = (size_t)cmd->blocks * cmd->blocklen;
	phys_bytes addr = cmd->data_phys;
	size_t left = total;
	unsigned n = 0;
	int op;

	if (!use_idmac || cmd->data_phys == 0 || total == 0)
		return EINVAL;
	if ((uint64_t)addr + total > 0x100000000ULL || (addr & 3) != 0)
		return EINVAL;
	if ((total + DW_DESC_MAX_LEN - 1) / DW_DESC_MAX_LEN > IDMAC_DESCS)
		return EINVAL;

	while (left > 0) {
		size_t len = (left > DW_DESC_MAX_LEN) ? DW_DESC_MAX_LEN : left;
		uint32_t des0 = DW_DES0_OWN | DW_DES0_CH;

		if (n == 0)
			des0 |= DW_DES0_FD;
		if (len == left)
			des0 |= DW_DES0_LD;
		else
			des0 |= DW_DES0_DIC;
		idmac_desc[n].des0 = des0;
		idmac_desc[n].des1 = (uint32_t)len;
		idmac_desc[n].des2 = (uint32_t)addr;
		idmac_desc[n].des3 = (uint32_t)(idmac_desc_phys +
		    (n + 1) * sizeof(struct dw_idmac_desc));
		addr += len;
		left -= len;
		n++;
	}

	op = (cmd->data_dir == SDMMC_DATA_WRITE) ?
	    CACHE_CLEAN : CACHE_INVALIDATE;
	if (cache_op(op, cmd->data, total) != OK)
		return EIO;
	if (cache_op(CACHE_CLEAN, idmac_desc,
	    n * sizeof(struct dw_idmac_desc)) != OK)
		return EIO;

	/*
	 * Linux's idmac_start_dma, in its order: the DMA interface reset in
	 * case the processor had the FIFO last, the engine's own reset, the
	 * list's address, then the engine selected and switched on. The poll
	 * demand that sets it fetching follows the command.
	 */
	if (reset_ctrl(DW_CTRL_DMA_RESET) != OK)
		return EIO;
	wr(DW_BMOD, rd(DW_BMOD) | DW_BMOD_SWR);
	wr(DW_IDSTS, 0xffffffffu);
	idma_active = 1;
	idma_done = 0;
	idma_error = 0;
	wr(DW_DBADDR, (uint32_t)idmac_desc_phys);
	wr(DW_CTRL, rd(DW_CTRL) | DW_CTRL_USE_IDMAC | DW_CTRL_DMA_ENABLE);
	wr(DW_BMOD, rd(DW_BMOD) | DW_BMOD_DE | DW_BMOD_FB);

	log_trace(&sdmmc_log, "idmac: %u desc for %u bytes at phys 0x%lx\n",
	    n, (unsigned)total, (unsigned long)cmd->data_phys);
	return OK;
}

/*
 * Take the engine out of the path again, so that a command the processor
 * serves next finds the FIFO its own. Linux's idmac_stop_dma, less the reset,
 * which this file keeps for recover().
 */
static void
dma_stop(void)
{
	wr(DW_BMOD, rd(DW_BMOD) & ~(DW_BMOD_DE | DW_BMOD_FB));
	wr(DW_CTRL, rd(DW_CTRL) & ~(DW_CTRL_USE_IDMAC | DW_CTRL_DMA_ENABLE));
	idma_active = 0;
}

/*
 * The programmed path: move the words through the FIFO as it fills or
 * empties. The part stops the card clock rather than overrun, so a slow
 * loop is a slow transfer and not a lost one; the deadline is on the whole.
 *
 * A read's "data transfer over" can arrive with words still in the FIFO -
 * it says the card is done, not that the processor is - so the loop runs
 * on the count, and the status is only looked at for errors.
 */
static int
pio_transfer(struct sdmmc_cmd *cmd)
{
	uint32_t *p = cmd->data;
	uint32_t left = (cmd->blocks * cmd->blocklen) / 4;
	uint32_t st, count, i;
	spin_t s;

	spin_init(&s, DATA_TIMEOUT_US);
	while (left > 0) {
		if (rd(DW_RINTSTS) & DW_INT_DATA_ERROR)
			return EIO;

		st = rd(DW_STATUS);
		count = DW_STATUS_FIFO_COUNT(st);
		if (cmd->data_dir == SDMMC_DATA_READ) {
			if (count > left)
				count = left;
			for (i = 0; i < count; i++)
				*p++ = rd(data_reg);
		} else {
			count = fifo_depth - count;
			if (count > left)
				count = left;
			for (i = 0; i < count; i++)
				wr(data_reg, *p++);
		}
		left -= count;

		if (count == 0 && !spin_check(&s))
			return ETIMEDOUT;
	}

	/* Those two only ever say "the FIFO wants you"; nobody waits on them. */
	wr(DW_RINTSTS, DW_INT_RXDR | DW_INT_TXDR);
	return OK;
}

/* Wait for the card to let go of DAT0, after a command with busy. */
static int
wait_not_busy(uint32_t usecs)
{
	spin_t s;

	spin_init(&s, usecs);
	do {
		if ((rd(DW_STATUS) & DW_STATUS_DATA_BUSY) == 0)
			return OK;
	} while (spin_check(&s));
	return ETIMEDOUT;
}

static int
dw_command(struct sdmmc_cmd *cmd)
{
	uint32_t cmdreg, st, i;
	int r, use_dma = 0, has_data;

	if (regs == NULL)
		return ENXIO;

	has_data = (cmd->data_dir != SDMMC_DATA_NONE);

	if ((r = wait_cmd_taken(CMD_TIMEOUT_US)) != OK) {
		log_warn(&sdmmc_log, "CMD%u: the part has not taken the last "
		    "command\n", cmd->index);
		dump_regs_at(LEVEL_WARN, "stuck");
		return r;
	}
	/*
	 * Nothing goes to a card that holds DAT0 low, except the two
	 * commands made for that state: SEND_STATUS, which asks how it is
	 * going, and STOP_TRANSMISSION.
	 *
	 * The first version waited only before commands with data or busy
	 * of their own, after the databook, and lost writes on the board: a
	 * CMD23 for the next run went out while the card was still
	 * programming the last one, the card - in its programming state -
	 * did not take it, and the CMD25 after it became an open-ended write
	 * that nothing ever stopped. Five megabytes of thirty-two never
	 * reached the card, and every command reported success.
	 */
	if (cmd->index != 12 && cmd->index != 13 &&
	    wait_not_busy(BUSY_TIMEOUT_US) != OK) {
		log_warn(&sdmmc_log, "CMD%u: the card is still busy\n",
		    cmd->index);
		return EBUSY;
	}

	wr(DW_RINTSTS, DW_INT_ALL);

	cmdreg = DW_CMD_START | DW_CMD_USE_HOLD_REG | DW_CMD_INDEX(cmd->index);

	/*
	 * Eighty clocks before the very first command, which the SD
	 * specification asks for after power-up and the part sends when
	 * told to.
	 */
	if (send_init) {
		cmdreg |= DW_CMD_INIT;
		send_init = 0;
	}

	/*
	 * A stop goes out at once, data or no data; everything else waits
	 * for the previous transfer to finish, except SEND_STATUS, which is
	 * how a transfer's progress is asked about. Linux's rule.
	 */
	if (cmd->index == 12 || cmd->index == 15)
		cmdreg |= DW_CMD_STOP_ABORT;
	else if (cmd->index != 13)
		cmdreg |= DW_CMD_PRV_DAT_WAIT;

	switch (cmd->rsp_type) {
	case SDMMC_RSP_NONE:
		break;
	case SDMMC_RSP_R2:
		cmdreg |= DW_CMD_RESP_EXP | DW_CMD_RESP_LONG | DW_CMD_RESP_CRC;
		break;
	case SDMMC_RSP_R3:
		cmdreg |= DW_CMD_RESP_EXP;	/* the OCR carries no CRC */
		break;
	default:
		cmdreg |= DW_CMD_RESP_EXP | DW_CMD_RESP_CRC;
		break;
	}

	if (has_data) {
		if (cmd->blocklen % 4 != 0 || ((uintptr_t)cmd->data & 3) != 0)
			return EINVAL;

		if ((r = reset_ctrl(DW_CTRL_FIFO_RESET)) != OK)
			return r;
		wr(DW_BLKSIZ, cmd->blocklen);
		wr(DW_BYTCNT, cmd->blocks * cmd->blocklen);

		cmdreg |= DW_CMD_DATA_EXP;
		if (cmd->data_dir == SDMMC_DATA_WRITE)
			cmdreg |= DW_CMD_WRITE;
		if (cmd->stop && cmd->blocks > 1)
			cmdreg |= DW_CMD_SEND_STOP;

		/*
		 * By the engine, or by the processor with the engine out of
		 * the path: with the interface still selected the FIFO
		 * belongs to the engine, and a programmed transfer waits for
		 * an end that never comes (see dw_init).
		 */
		if (dma_setup(cmd) == OK)
			use_dma = 1;
		else
			dma_stop();
	}

	wr(DW_CMDARG, cmd->arg);
	log_debug(&sdmmc_log, "CMD%u: cmd %08x arg %08x%s\n", cmd->index,
	    cmdreg, cmd->arg, use_dma ? " dma" : "");
	wr(DW_CMD, cmdreg);
	if (use_dma)
		wr(DW_PLDMND, 1);

	r = wait_intr(DW_INT_CMD_DONE, DW_INT_CMD_ERROR, CMD_TIMEOUT_US, &st);
	if (r == OK && (st & DW_INT_CMD_ERROR))
		r = EIO;
	if (r != OK) {
		if (use_dma)
			dma_stop();
		if (r == EIO)
			return command_error(cmd, st);
		log_warn(&sdmmc_log, "CMD%u: no completion\n", cmd->index);
		dump_regs_at(LEVEL_WARN, "no completion");
		recover();
		return r;
	}
	wr(DW_RINTSTS, DW_INT_CMD_DONE);

	if (cmd->rsp_type == SDMMC_RSP_R2) {
		/*
		 * Whole, CRC byte and all: RESP0 is bits 31:0, RESP3 bits
		 * 127:96. The low byte is cleared to match what the other
		 * host gives the card layer, where it reads as zero.
		 */
		for (i = 0; i < 4; i++)
			cmd->resp[i] = rd(DW_RESP0 + 4 * i);
		log_debug(&sdmmc_log, "CMD%u: raw %08x %08x %08x %08x\n",
		    cmd->index, cmd->resp[3], cmd->resp[2], cmd->resp[1],
		    cmd->resp[0]);
		cmd->resp[0] &= ~0xffu;
	} else if (cmd->rsp_type != SDMMC_RSP_NONE) {
		cmd->resp[0] = rd(DW_RESP0);
		log_debug(&sdmmc_log, "CMD%u: raw %08x\n", cmd->index,
		    cmd->resp[0]);
	}

	if (has_data) {
		if (!use_dma && (r = pio_transfer(cmd)) != OK) {
			st = rd(DW_RINTSTS);
			log_warn(&sdmmc_log, "CMD%u: programmed transfer "
			    "failed (%d), %u bytes counted\n", cmd->index, r,
			    rd(DW_TCBCNT));
			return (r == EIO) ? command_error(cmd, st) :
			    (recover(), r);
		}

		r = wait_intr(DW_INT_DTO, DW_INT_DATA_ERROR, DATA_TIMEOUT_US,
		    &st);
		if (r != OK && use_dma)
			dma_stop();
		if (r == EIO)
			return command_error(cmd, st);
		if (r != OK) {
			log_warn(&sdmmc_log, "CMD%u: transfer never "
			    "completed (rint %08x, engine %s)\n", cmd->index,
			    st, !use_dma ? "not used" :
			    idma_done ? "done" : "silent");
			dump_regs_at(LEVEL_WARN, "transfer");
			recover();
			return r;
		}
		/*
		 * The wait above asked for the card and the engine both
		 * (check_intr); what is left is to take the engine out of the
		 * path and say why, if it complained.
		 */
		if (use_dma) {
			uint32_t e = idma_error;

			dma_stop();
			if (e != 0) {
				log_warn(&sdmmc_log, "CMD%u: the DMA engine "
				    "reports 0x%08x\n", cmd->index, e);
				return command_error(cmd, st);
			}
		}
		wr(DW_RINTSTS, DW_INT_DTO);
		wr(DW_IDSTS, 0xffffffffu);

		/* The stop the part sent by itself has its own completion. */
		if (cmdreg & DW_CMD_SEND_STOP) {
			if (poll_intr(DW_INT_ACD, 0, CMD_TIMEOUT_US, &st) != OK)
				log_warn(&sdmmc_log, "CMD%u: the automatic "
				    "stop never completed\n", cmd->index);
			wr(DW_RINTSTS, DW_INT_ACD);
		}

		/*
		 * And a write is over when the card has programmed it, not
		 * when the last block has crossed the bus: DTO on this part
		 * comes before the card lets go of DAT0. The SD Host
		 * Controller holds its transfer-complete until then, and the
		 * card layer was written against that; waiting here gives it
		 * the same meaning on this host.
		 */
		if (cmd->data_dir == SDMMC_DATA_WRITE &&
		    wait_not_busy(BUSY_TIMEOUT_US) != OK) {
			log_warn(&sdmmc_log, "CMD%u: the card stayed busy after "
			    "the write\n", cmd->index);
			return ETIMEDOUT;
		}

		/* After the device is done; see sdmmc_sdhci.c. */
		if (use_dma && cmd->data_dir == SDMMC_DATA_READ)
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

/*
 * Is there a card in the socket at all?
 *
 * The part's own detect input, unless the tree says the slot cannot be
 * empty ("non-removable"), that the input is not wired ("broken-cd"), or
 * that detection is a GPIO ("cd-gpios") - a pin this driver has no reader
 * for, so the card is assumed present and identification decides.
 */
static int
card_present(void)
{
	if (non_removable || broken_cd)
		return 1;
	if (gpio_cd) {
		log_info(&sdmmc_log, "card detect is a GPIO this driver does "
		    "not read; assuming a card\n");
		return 1;
	}
	return (rd(DW_CDETECT) & DW_CDETECT_ABSENT) == 0;
}

static int
dw_init(void)
{
	uint32_t verid, hcon, fifoth, msize;
	unsigned width;
	void *v;
	int r;

	/*
	 * The bus clock before the registers: on a Rockchip part a register
	 * read with the interface clock stopped does not fail, it hangs the
	 * bus. The firmware leaves it running; this makes sure.
	 */
	(void)sdmmc_clk_enable(&clk_biu);
	(void)sdmmc_clk_enable(&clk_ciu);

	if (regs == NULL) {
		v = vm_map_phys(SELF, (void *)reg_base, reg_size);
		if (v == MAP_FAILED) {
			log_warn(&sdmmc_log, "cannot map registers at 0x%lx; "
			    "not granted by RS?\n", (unsigned long)reg_base);
			return EPERM;
		}
		regs = v;
	}

	/* What the bootloader left, before any of it is undone. */
	dump_regs_at(LEVEL_INFO, "as found");

	sdmmc_reset_pulse(rst, rst_n);

	verid = rd(DW_VERID) & 0xffff;
	hcon = rd(DW_HCON);
	data_reg = (verid < DW_VERID_240A) ? DW_DATA_OLD : DW_DATA_240A;

	/*
	 * The host data width, HCON bits 9:7: 0 sixteen, 1 thirty-two, 2
	 * sixty-four bits. The FIFO is read a word of that width at a time,
	 * and this driver reads 32-bit words.
	 */
	width = (hcon >> 7) & 0x7;
	if (width != 1) {
		log_warn(&sdmmc_log, "the part has a %s-bit host bus, and this "
		    "driver moves 32-bit words\n", width == 0 ? "16" : "64");
		return ENOTSUP;
	}

	/*
	 * CTRL is written whole before it is reset, because the reset bits
	 * clear themselves and nothing else: the interface selection beside
	 * them survives. The firmware hands the part over with the internal
	 * engine selected and enabled (the vendor system reads 0x02000030),
	 * the hardware reset line does not always reach it either, and with
	 * it left on the first transfer through the FIFO - the SCR, eight
	 * bytes - goes to an engine nobody started and never ends. Every
	 * later instance worked, because the one before had cleared it on
	 * its way out; only the first after boot failed, which is the one rc
	 * starts.
	 */
	wr(DW_CTRL, 0);
	wr(DW_BMOD, DW_BMOD_SWR);
	wr(DW_PWREN, 1);
	if ((r = reset_ctrl(DW_CTRL_ALL_RESET)) != OK)
		return r;

	wr(DW_RINTSTS, DW_INT_ALL);
	wr(DW_INTMASK, 0);
	wr(DW_IDSTS, 0xffffffffu);
	wr(DW_IDINTEN, DW_IDSTS_TI | DW_IDSTS_RI | DW_IDSTS_FBE | DW_IDSTS_DU |
	    DW_IDSTS_CES | DW_IDSTS_NIS | DW_IDSTS_AIS);
	wr(DW_TMOUT, 0xffffffffu);

	/*
	 * FIFO thresholds: half the FIFO each way, and bursts of sixteen
	 * words - which divide both halves, as the databook requires of the
	 * burst size. The depth comes from the tree ("fifo-depth", 256 words
	 * on this board) or, failing that, from the power-on value of the
	 * receive threshold, which is depth - 1.
	 */
	if (fifo_depth == 0)
		fifo_depth = ((rd(DW_FIFOTH) >> DW_FIFOTH_RX_SHIFT) &
		    DW_FIFOTH_RX_MASK) + 1;
	msize = 3;	/* 16 */
	fifoth = (msize << DW_FIFOTH_MSIZE_SHIFT) |
	    ((fifo_depth / 2 - 1) << DW_FIFOTH_RX_SHIFT) | (fifo_depth / 2);
	wr(DW_FIFOTH, fifoth);

	/*
	 * The internal engine: only if the part was built with it (HCON's
	 * DMA interface field is zero) and with 32-bit addresses, which is
	 * the descriptor format written here. A part with a 64-bit engine
	 * would read these descriptors as garbage, not fail politely.
	 */
	if (idmac_desc == NULL)
		idmac_desc = alloc_contig(IDMAC_TABLE_BYTES, AC_ALIGN4K,
		    &idmac_desc_phys);
	use_idmac = 0;
	if (DW_HCON_DMA_IF(hcon) != 0)
		log_info(&sdmmc_log, "the part has no internal DMA engine; "
		    "the processor will move the data\n");
	else if (DW_HCON_ADDR64(hcon))
		log_info(&sdmmc_log, "the part's DMA engine takes 64-bit "
		    "descriptors, which this driver does not write; the "
		    "processor will move the data\n");
	else if (idmac_desc == NULL || idmac_desc_phys == 0 ||
	    idmac_desc_phys + IDMAC_TABLE_BYTES > 0x100000000ULL)
		log_warn(&sdmmc_log, "no descriptor table below 4 GiB; the "
		    "processor will move the data\n");
	else
		use_idmac = 1;

	if (this_host != NULL) {
		/*
		 * The FIFO holds the card back rather than overflowing, so
		 * the programmed path takes what it is given; eight blocks
		 * keeps one command's worth of polling short.
		 */
		this_host->max_blocks_pio = 8;
		this_host->max_blocks = use_idmac ?
		    (unsigned)(IDMAC_DESCS * DW_DESC_MAX_LEN /
		    SDMMC_SECTOR_SIZE) : this_host->max_blocks_pio;
	}

	/* The interrupt, if the tree named one and RS granted it. */
	if (irq_line >= 0 && !irq_ok) {
		irq_hook = SDMMC_IRQ_HOOK;	/* not the line; sdmmc_sdhci.c */
		if ((r = sys_irqsetpolicy(irq_line, 0, &irq_hook)) != OK)
			log_warn(&sdmmc_log, "line %d not granted (%d); every "
			    "wait will be a poll\n", irq_line, r);
		else
			irq_ok = 1;
	}

	/*
	 * Signalled: the two events a wait sleeps on, and every error. Not
	 * the FIFO's requests (levels, and the programmed path polls them),
	 * not card detect, not SDIO. The part latches every status bit in
	 * RINTSTS whatever the mask says, so polling sees them all anyway.
	 */
	if (irq_ok) {
		wr(DW_INTMASK, DW_INT_CMD_DONE | DW_INT_DTO |
		    DW_INT_CMD_ERROR | DW_INT_DATA_ERROR);
		wr(DW_CTRL, rd(DW_CTRL) | DW_CTRL_INT_ENABLE);
	}

	send_init = 1;
	(void)dw_set_bus_width(1);
	(void)dw_set_timing(0);
	if ((r = dw_set_clock(SDMMC_IDENT_HZ, NULL)) != OK)
		return r;

	log_info(&sdmmc_log, "DesignWare MSHC %x.%02xa at 0x%lx, FIFO %u "
	    "words, input %u Hz / %u\n", verid >> 12, (verid >> 4) & 0xff,
	    (unsigned long)reg_base, fifo_depth, ciu_hz, variant->ciu_div);
	log_info(&sdmmc_log, "transfers by %s, waits by %s\n",
	    use_idmac ? "internal DMA" : "the processor",
	    irq_ok ? "interrupt" : "polling");

	if (!card_present()) {
		log_info(&sdmmc_log, "the socket at 0x%lx is empty\n",
		    (unsigned long)reg_base);
		return ENODEV;
	}

	dump_regs("after init");
	return OK;
}

static void
dw_exit(void)
{
	if (regs != NULL) {
		wr(DW_INTMASK, 0);
		wr(DW_CTRL, rd(DW_CTRL) & ~DW_CTRL_INT_ENABLE);
		(void)dw_set_clock(0, NULL);
		vm_unmap_phys(SELF, (void *)regs, reg_size);
	}
	if (irq_ok) {
		(void)sys_irqrmpolicy(&irq_hook);
		irq_ok = 0;
	}
	regs = NULL;
}

static uint32_t
prop_u32(const struct fdt_node *node, const char *name, uint32_t dflt)
{
	const void *p;
	unsigned len;

	if ((p = fdt_getprop(node, name, &len)) == NULL || len < 4)
		return dflt;
	return (uint32_t)fdt_read_cells(p, 1);
}

int
dwmshc_probe(const struct fdt_node *node, const struct sdmmc_devinfo *info,
	struct sdmmc_host *host)
{
	unsigned i;

	if (info->size < 0x200)
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
	non_removable = info->non_removable;
	broken_cd = (fdt_getprop(node, "broken-cd", NULL) != NULL);
	gpio_cd = (fdt_getprop(node, "cd-gpios", NULL) != NULL);
	fifo_depth = prop_u32(node, "fifo-depth", 0);
	sample_phase = prop_u32(node, "rockchip,default-sample-phase", 0);

	rst_n = info->nresets;
	if (rst_n > SDMMC_MAX_RESETS)
		rst_n = SDMMC_MAX_RESETS;
	memcpy(rst, info->reset, sizeof(rst));

	/*
	 * "biu" is the bus interface, "ciu" the card interface - the clock
	 * the card clock is divided from - in every binding of this part.
	 * The phase clocks are Rockchip's.
	 */
	if (sdmmc_clk_get(node, "biu", &clk_biu) != OK)
		clk_biu.ctl = SDMMC_NO_LINE;
	if (sdmmc_clk_get(node, "ciu", &clk_ciu) != OK)
		clk_ciu.ctl = SDMMC_NO_LINE;
	if (sdmmc_clk_get(node, "ciu-drive", &clk_drive) != OK)
		clk_drive.ctl = SDMMC_NO_LINE;
	if (sdmmc_clk_get(node, "ciu-sample", &clk_sample) != OK)
		clk_sample.ctl = SDMMC_NO_LINE;

	/* The fallback input rate, if the clock cannot be set; see above. */
	ciu_hz = (info->max_freq != 0 ? info->max_freq : 50000000) *
	    variant->ciu_div;

	host->init = dw_init;
	host->exit = dw_exit;
	host->set_clock = dw_set_clock;
	host->set_bus_width = dw_set_bus_width;
	host->set_timing = dw_set_timing;
	host->command = dw_command;
	host->max_bus_width = info->bus_width;
	host->max_freq = info->max_freq;
	host->max_blocks = 8;
	host->max_blocks_pio = 8;
	return OK;
}
