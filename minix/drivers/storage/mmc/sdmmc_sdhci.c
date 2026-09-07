/*
 * The SD Host Controller standard register set, as the RK3566 eMMC has it.
 *
 * The node is "rockchip,rk3568-dwcmshc" / "rockchip,dwcmshc-sdhci": a
 * Synopsys DesignWare Cores Mobile Storage Host Controller, which despite
 * the name is not the "dw-mshc" that mmc@fe2b0000 is. dw-mshc is Synopsys's
 * own register set - CTRL, CLKDIV, CLKENA and a clock that changes by
 * command rather than by writing the divider. dwcmshc is the SD Host
 * Controller Simplified Specification, the same registers a PCI card reader
 * has, plus two vendor registers. So this file is a plain SDHCI driver and
 * sdhcreg.h describes almost all of it; sdmmc_sdhci.h has the rest.
 *
 * Data moves through the FIFO by the CPU, not by the controller's DMA. That
 * is deliberate and is written up in port/PORTING-LOG.md: a DMA engine that
 * is not coherent with the caches needs the buffer cleaned before a write
 * and invalidated after a read, and this port has no way for a driver to ask
 * for either - cache maintenance exists only inside the kernel. Doing it
 * with a non-cacheable mapping instead does not work here either, because
 * the kernel maps all of RAM cacheably and sys_safecopy reads the buffer
 * through that mapping: the two aliases would disagree. PIO has no such
 * question, and QEMU cannot show the difference, so guessing was not an
 * option. See "Кэши и DMA" in the log.
 *
 * Interrupts are enabled in the status registers but not signalled to the
 * GIC: every wait here is a poll with a deadline. One unknown at a time -
 * the interrupt path can be added without changing anything a caller sees,
 * and the line is already granted by RS from the tree.
 */

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
#include "sdhcreg.h"
#include "sdmmc_sdhci.h"

/*
 * Deadlines. A command that has not answered in a second is not slow, it is
 * not there; a data transfer of at most 64 blocks at the lowest clock this
 * driver ever uses takes well under a second, so five is generous. Busy
 * after a write or an erase is the one that genuinely takes its time - the
 * specification allows an erase to run for minutes, but nothing this driver
 * issues erases, so ten seconds bounds the wait without hiding a hang.
 */
#define CMD_TIMEOUT_US		1000000
#define DATA_TIMEOUT_US		5000000
#define BUSY_TIMEOUT_US		10000000
#define RESET_TIMEOUT_US	100000
#define CLOCK_TIMEOUT_US	100000

/*
 * Whether the controller is asked to check the CRC and index of a response.
 *
 * On by default, because a response that fails its CRC is not a response.
 * Switchable because on first contact with this part every command carrying
 * a check went unanswered while the one without any was answered every
 * time, and telling "the checks are broken" from "the bits are wrong and
 * the checks are right" is one boot with them off: a correct CID means the
 * former, a corrupt one means the latter.
 */
static int check_responses = 1;

/* Where the registers are, and where they ended up. */
static phys_bytes reg_base;
static size_t reg_size;
static vir_bytes regs;

/*
 * The reset controller: where it is, where it ended up, and which of its
 * lines belong to this controller. Empty on a machine whose tree names none,
 * which is every machine but this board so far.
 */
static phys_bytes rst_base;
static size_t rst_size;
static vir_bytes rst_regs;
static unsigned rst_n;
static unsigned rst_id[SDMMC_MAX_RESETS];

/*
 * Rockchip's software reset registers: sixteen lines each, from 0x400 up,
 * and every write carries its own mask in the top half so that one line can
 * be changed without reading the register first. Line n lives in register
 * n / 16 at bit n % 16.
 */
#define RK_SOFTRST_CON0		0x400
#define RK_SOFTRST_PER_REG	16

/*
 * The card clock's source, which on this SoC is a multiplexer and not a
 * divider - and getting that wrong is not a slow card but a wrong one.
 *
 * With the delay line bypassed the controller samples what the card drives
 * against the *source* clock, not against the divided card clock. Leave the
 * source at 198 MHz and divide it by 534 for identification and the card is
 * clocked correctly while its answers are sampled at the wrong instants:
 * a 48-bit response comes back with bits missing but still looking like a
 * plausible number, and a 136-bit one does not come back at all. That is
 * how this driver spent an afternoon believing an eMMC had the operating
 * range 0x003f8000 when it was really answering 0x40ff8080.
 *
 * Hence the mux, and hence 375 kHz: it is not "at most 400" rounded down,
 * it is the one sub-megahertz input the multiplexer has. CLKSEL_CON28 bits
 * 14:12 choose between these six, and every Rockchip register takes its own
 * write mask in the top half so one field can be set without a read.
 */
#define RK_CLKSEL_CON28		0x170
#define RK_CCLK_EMMC_SHIFT	12
#define RK_CCLK_EMMC_MASK	0x7

static const struct { uint32_t hz; unsigned sel; } cclk_emmc[] = {
	{    375000, 5 },
	{  24000000, 0 },
	{  50000000, 4 },
	{ 100000000, 3 },
	{ 150000000, 2 },
	{ 200000000, 1 }
};

/* Read out of the part at init, not written down here. */
static uint32_t base_clock;		/* the divider's input, in Hz */
static unsigned spec_version;
static int is_dwcmshc;			/* the Rockchip part: vendor area */
static unsigned vendor_area;

static uint8_t
rd8(unsigned off)
{
	return *(volatile uint8_t *)(regs + off);
}

static uint16_t
rd16(unsigned off)
{
	return *(volatile uint16_t *)(regs + off);
}

static uint32_t
rd32(unsigned off)
{
	return *(volatile uint32_t *)(regs + off);
}

static void
wr8(unsigned off, uint8_t v)
{
	*(volatile uint8_t *)(regs + off) = v;
}

static void
wr16(unsigned off, uint16_t v)
{
	*(volatile uint16_t *)(regs + off) = v;
}

static void
wr32(unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(regs + off) = v;
}

/*
 * Spin until every bit of mask is clear in the present state, or the time
 * runs out. Returns OK or EBUSY.
 */
static int
wait_state_clear(uint32_t mask, uint32_t usecs)
{
	spin_t s;

	spin_init(&s, usecs);
	do {
		if ((rd32(SDHC_PRESENT_STATE) & mask) == 0)
			return OK;
	} while (spin_check(&s));

	return EBUSY;
}

/*
 * Spin until one of the wanted normal-interrupt bits is set, or an error is
 * flagged, or the time runs out.
 *
 * The bit is not cleared here: which of several bits arrived is the caller's
 * business, and a data transfer wants to clear buffer-ready without touching
 * transfer-complete.
 */
static int
wait_intr(uint16_t wanted, uint32_t usecs, uint16_t *got)
{
	spin_t s;
	uint16_t n;

	spin_init(&s, usecs);
	do {
		n = rd16(SDHC_NINTR_STATUS);
		/*
		 * The error is looked at first on purpose: a transfer that
		 * ends badly can raise the bit the caller is waiting for in
		 * the same status word, and taking that as success would
		 * turn a CRC error into data.
		 */
		if (n & SDHC_ERROR_INTERRUPT)
			return EIO;
		if (n & wanted) {
			if (got != NULL)
				*got = n;
			return OK;
		}
	} while (spin_check(&s));

	return ETIMEDOUT;
}

/*
 * The registers worth seeing when something has gone wrong, in one line.
 *
 * This is for the first contact with a part nobody here has driven: on a
 * machine where the driver does not get as far as a card, the difference
 * between "the registers are not mapped", "the part is held in reset" and
 * "the clock never started" is entirely in these numbers, and reading them
 * out of a hung board afterwards is not possible.
 */
static void
dump_regs(const char *when)
{
	/*
	 * Two short lines rather than one long one. The board's console runs
	 * at 1500000 and reaches this workstation over a USB bridge that
	 * cannot take it: long lines come back with a hole in the middle,
	 * and a register dump with a hole in it is worse than none, because
	 * it reads as a plausible number.
	 */
	log_debug(&sdmmc_log, "%s: st %08x ctl %02x pwr %02x clk %04x "
	    "to %02x misc %08x\n", when, rd32(SDHC_PRESENT_STATE),
	    rd8(SDHC_HOST_CTL), rd8(SDHC_POWER_CTL), rd16(SDHC_CLOCK_CTL),
	    rd8(SDHC_TIMEOUT_CTL),
	    is_dwcmshc ? rd32(DWCMSHC_EMMC_MISC_CON) : 0);
	log_debug(&sdmmc_log, "%s: nis %04x eis %04x nie %04x eie %04x "
	    "ctl2 %04x ver %04x\n", when, rd16(SDHC_NINTR_STATUS),
	    rd16(SDHC_EINTR_STATUS), rd16(SDHC_NINTR_STATUS_EN),
	    rd16(SDHC_EINTR_STATUS_EN), rd16(SDHC_HOST_CTL2),
	    rd16(SDHC_HOST_CTL_VERSION));
	if (is_dwcmshc)
		log_debug(&sdmmc_log, "%s: dll ctl %08x rx %08x tx %08x "
		    "strbin %08x cmdout %08x\n", when,
		    rd32(DWCMSHC_EMMC_DLL_CTRL), rd32(DWCMSHC_EMMC_DLL_RXCLK),
		    rd32(DWCMSHC_EMMC_DLL_TXCLK),
		    rd32(DWCMSHC_EMMC_DLL_STRBIN),
		    rd32(DWCMSHC_EMMC_DLL_CMDOUT));
}

/*
 * Reset part of the controller and wait for the bit to fall.
 *
 * SDHC_RESET_ALL puts every register back to its power-on value, so init
 * has to redo power, clock, timeout and the interrupt enables after it;
 * SDHC_RESET_CMD and SDHC_RESET_DAT are the ones an error path uses, and
 * they leave the rest alone.
 */
/*
 * Pull the controller's reset lines, if the tree gave it any.
 *
 * The software reset of the standard reaches the standard registers and
 * nothing else; the vendor half of this part - the delay line, the clock
 * enable, whatever mode the bootloader left it in - is behind these lines.
 * Both the vendor kernel and mainline pull them around a full reset
 * whenever the description provides them, and the board's own tree does.
 */
static void
hardware_reset(void)
{
	unsigned i, reg, bit;
	uint32_t v;

	if (rst_regs == 0 || rst_n == 0)
		return;

	for (i = 0; i < rst_n; i++) {
		reg = RK_SOFTRST_CON0 + (rst_id[i] / RK_SOFTRST_PER_REG) * 4;
		bit = rst_id[i] % RK_SOFTRST_PER_REG;
		if (reg + 4 > rst_size)
			continue;
		v = (1u << (bit + 16)) | (1u << bit);
		*(volatile uint32_t *)(rst_regs + reg) = v;
	}

	micro_delay(10);

	for (i = 0; i < rst_n; i++) {
		reg = RK_SOFTRST_CON0 + (rst_id[i] / RK_SOFTRST_PER_REG) * 4;
		bit = rst_id[i] % RK_SOFTRST_PER_REG;
		if (reg + 4 > rst_size)
			continue;
		v = (1u << (bit + 16));		/* mask set, value clear */
		*(volatile uint32_t *)(rst_regs + reg) = v;
	}

	micro_delay(100);
}

static int
reset(uint8_t mask)
{
	uint32_t misc = 0;
	spin_t s;
	int r = EIO;

	/*
	 * On the Rockchip part a reset also clears the bit that lets the
	 * internal clock run, and the register it lives in is outside the
	 * standard block, so the reset does not put it back. Saved here and
	 * restored below, which is what Linux does around the same reset.
	 * Skipped before the registers are known to be readable.
	 */
	if (is_dwcmshc && regs != 0)
		misc = rd32(DWCMSHC_EMMC_MISC_CON);

	if (mask & SDHC_RESET_ALL)
		hardware_reset();

	wr8(SDHC_SOFTWARE_RESET, mask);

	spin_init(&s, RESET_TIMEOUT_US);
	do {
		if ((rd8(SDHC_SOFTWARE_RESET) & mask) == 0) {
			r = OK;
			break;
		}
	} while (spin_check(&s));

	if (is_dwcmshc && regs != 0)
		wr32(DWCMSHC_EMMC_MISC_CON, misc | DWCMSHC_MISC_INTCLK_EN);

	if (r != OK)
		log_warn(&sdmmc_log, "reset 0x%x did not complete\n", mask);
	return r;
}

/*
 * The card clock.
 *
 * From specification 3.00 the divider is ten bits: the register holds N, the
 * divisor is 2N, and N == 0 means the base clock undivided. Rockchip's part
 * is documented as not accepting N == 0, and nothing here wants it - the
 * base is 198 MHz on this board and the fastest mode this driver uses is
 * 52 MHz - so the search starts at N = 1.
 */
/*
 * Point the card clock's multiplexer at the lowest source that a whole
 * divider can bring down to hz, and answer with that source's rate. Zero
 * when there is no reset controller mapped - which is the same register
 * block - and the caller then divides whatever the part came up with.
 */
static uint32_t
set_source_clock(uint32_t hz)
{
	unsigned i, best = 0;
	uint32_t v, best_got = 0;

	if (rst_regs == 0 || RK_CLKSEL_CON28 + 4 > rst_size || hz == 0)
		return 0;

	/*
	 * The best source is the one whose highest achievable rate at or
	 * below hz is greatest: a source at or below hz is used undivided,
	 * a faster one is divided down.
	 */
	for (i = 0; i < sizeof(cclk_emmc) / sizeof(cclk_emmc[0]); i++) {
		uint32_t cand = cclk_emmc[i].hz;
		uint32_t got = (cand <= hz) ? cand :
		    cand / (2 * ((cand + 2 * hz - 1) / (2 * hz)));

		if (got <= hz && got > best_got) {
			best = i;
			best_got = got;
		}
	}

	v = ((uint32_t)RK_CCLK_EMMC_MASK << (RK_CCLK_EMMC_SHIFT + 16)) |
	    ((uint32_t)cclk_emmc[best].sel << RK_CCLK_EMMC_SHIFT);
	*(volatile uint32_t *)(rst_regs + RK_CLKSEL_CON28) = v;

	log_debug(&sdmmc_log, "card clock source %u Hz (mux %u) for %u Hz\n",
	    cclk_emmc[best].hz, cclk_emmc[best].sel, hz);
	return cclk_emmc[best].hz;
}

static int
sdhci_set_clock(uint32_t hz, uint32_t *actual)
{
	uint32_t n, got, src;
	uint16_t clk;
	spin_t s;

	/* Stop the clock before touching the divider, as the standard says. */
	wr16(SDHC_CLOCK_CTL, rd16(SDHC_CLOCK_CTL) & ~SDHC_SDCLK_ENABLE);

	if (hz == 0) {
		wr16(SDHC_CLOCK_CTL, 0);
		if (actual != NULL)
			*actual = 0;
		return OK;
	}

	/*
	 * The source first, because it decides both the rate the divider
	 * works from and the clock the responses are sampled against.
	 */
	if (is_dwcmshc && (src = set_source_clock(hz)) != 0)
		base_clock = src;

	if (base_clock <= hz) {
		/* The source is already at or below the wish: no divider. */
		n = 0;
		got = base_clock;
	} else {
		/* Smallest N with base/(2N) <= hz, which is ceil(base/2hz). */
		n = (base_clock + 2 * hz - 1) / (2 * hz);
		if (n > 0x3ff)
			n = 0x3ff;
		got = base_clock / (2 * n);
	}

	clk = (uint16_t)(((n & 0xff) << SDHC_SDCLK_DIV_SHIFT) |
	    (((n >> 8) & SDHC_SDCLK_XDIV_MASK) << SDHC_SDCLK_XDIV_SHIFT) |
	    SDHC_INTCLK_ENABLE);
	wr16(SDHC_CLOCK_CTL, clk);

	spin_init(&s, CLOCK_TIMEOUT_US);
	while ((rd16(SDHC_CLOCK_CTL) & SDHC_INTCLK_STABLE) == 0) {
		if (!spin_check(&s)) {
			/*
			 * If this ever fires on the Rockchip part, the thing
			 * to try is bit 1 of its MISC_CON register at 0x81c,
			 * which recent Linux sets after every reset and calls
			 * "enable internal clock". U-Boot's driver for the
			 * same silicon does not touch it, which is why this
			 * driver does not either - but that is an argument
			 * from precedent, not from the manual.
			 */
			log_warn(&sdmmc_log, "internal clock never settled\n");
			return EIO;
		}
	}

	wr16(SDHC_CLOCK_CTL, clk | SDHC_SDCLK_ENABLE);

	/*
	 * And the sampling phase, which on the Rockchip part is not part of
	 * the standard block and therefore survives every reset. See
	 * sdmmc_sdhci.h: the delay line is put in bypass for every speed
	 * this driver uses, because training it is only needed above
	 * 52 MHz and a phase left over from the bootloader's HS200 makes
	 * the controller read an idle bus as a valid response.
	 */
	if (is_dwcmshc && got <= 52000000) {
		wr32(DWCMSHC_EMMC_DLL_CTRL,
		    DWCMSHC_DLL_BYPASS | DWCMSHC_DLL_START);
		wr32(DWCMSHC_EMMC_DLL_RXCLK, DWCMSHC_RXCLK_ORI_GATE);
		wr32(DWCMSHC_EMMC_DLL_TXCLK, 0);
		wr32(DWCMSHC_EMMC_DLL_CMDOUT, 0);
		wr32(DWCMSHC_EMMC_DLL_STRBIN, DWCMSHC_DLL_DLYENA |
		    DWCMSHC_STRBIN_DELAY_SEL |
		    (DWCMSHC_STRBIN_DELAY_DEFAULT << DWCMSHC_STRBIN_DELAY_SHIFT));
	}

	if (actual != NULL)
		*actual = got;
	log_debug(&sdmmc_log, "clock %u Hz asked, %u Hz set (base %u, N %u)\n",
	    hz, got, base_clock, n);
	return OK;
}

static int
sdhci_set_bus_width(unsigned bits)
{
	uint8_t ctl;

	ctl = rd8(SDHC_HOST_CTL) & ~(SDHC_4BIT_MODE | SDHC_8BIT_MODE);

	switch (bits) {
	case 1:
		break;
	case 4:
		ctl |= SDHC_4BIT_MODE;
		break;
	case 8:
		ctl |= SDHC_8BIT_MODE;
		break;
	default:
		return EINVAL;
	}

	wr8(SDHC_HOST_CTL, ctl);
	return OK;
}

/*
 * High speed timing, which on a standard part is one bit of Host Control 1.
 *
 * Nothing here touches Host Control 2: the modes it selects - SDR50, HS200,
 * HS400 - all need the part's delay line trained against the card, and that
 * is a body of code with nothing to check it against on this port. The card
 * is driven at 52 MHz in the plain high speed mode both an eMMC and an SD
 * card have had since 2004.
 */
static int
sdhci_set_timing(int high_speed)
{
	uint8_t ctl;

	ctl = rd8(SDHC_HOST_CTL);
	if (high_speed)
		ctl |= SDHC_HIGH_SPEED;
	else
		ctl &= ~SDHC_HIGH_SPEED;
	wr8(SDHC_HOST_CTL, ctl);
	return OK;
}

/*
 * Turn an error interrupt into an errno, and put the controller back in a
 * state where the next command can be issued.
 *
 * A command timeout is not always a fault: asking an eMMC for the SD
 * interface condition (CMD8) or an SD card for the MMC operating condition
 * (CMD1) is how the card layer finds out which kind of card it has, and the
 * answer is silence. So that one case gets its own errno and no complaint.
 */
static int
command_error(struct sdmmc_cmd *cmd)
{
	uint16_t e, acmd;

	e = rd16(SDHC_EINTR_STATUS);
	acmd = rd16(SDHC_CMD12_ERROR_STATUS);
	wr16(SDHC_EINTR_STATUS, e);
	wr16(SDHC_NINTR_STATUS, 0xffff);

	/*
	 * Both lines, whatever the error was.
	 *
	 * The first version reset the command line for command errors and
	 * the data line for data errors, which reads sensibly and leaves the
	 * controller wedged: an Auto CMD12 failure is neither, so nothing
	 * was reset, and the data line stayed inhibited for every request
	 * after it - one bad read turned into a dead device. The standard's
	 * own recovery procedure resets both, and there is nothing to save
	 * by being cleverer once a transfer has already gone wrong.
	 */
	(void)reset(SDHC_RESET_CMD);
	(void)reset(SDHC_RESET_DAT);

	if (e == SDHC_CMD_TIMEOUT_ERROR) {
		/*
		 * Not always a fault - this is how the card layer learns
		 * which kind of card it has - so it is not a warning. It is
		 * a debug line rather than a trace one because on a board
		 * nobody has driven before it is the difference between
		 * "the card said nothing" and "the controller is not
		 * driving the bus", and those look the same from above.
		 */
		log_debug(&sdmmc_log, "CMD%u arg %08x: no answer (eis %04x)\n",
		    cmd->index, cmd->arg, e);
		dump_regs("no answer");
		return ETIMEDOUT;
	}

	log_warn(&sdmmc_log, "CMD%u: error status 0x%04x, auto-cmd 0x%04x\n",
	    cmd->index, e, acmd);
	return EIO;
}

/*
 * Wait out the busy period of a command that answers with R1b.
 *
 * Two things say it is over, and not every part says both. The standard has
 * Transfer Complete set when a command with busy finishes; it also has
 * Command Inhibit (DAT), which is raised the moment such a command is issued
 * and dropped when the card releases DAT0. Taking whichever arrives first
 * costs nothing and avoids sitting out the whole timeout on a part that only
 * reports one of them - which would be ten seconds on every SWITCH, and a
 * SWITCH is what a cache flush is.
 */
static int
wait_busy_end(uint32_t usecs)
{
	spin_t s;
	uint16_t n;

	spin_init(&s, usecs);
	do {
		n = rd16(SDHC_NINTR_STATUS);
		if (n & SDHC_ERROR_INTERRUPT)
			return EIO;
		if (n & SDHC_TRANSFER_COMPLETE) {
			wr16(SDHC_NINTR_STATUS, SDHC_TRANSFER_COMPLETE);
			return OK;
		}
		if ((rd32(SDHC_PRESENT_STATE) & SDHC_CMD_INHIBIT_DAT) == 0)
			return OK;
	} while (spin_check(&s));

	return ETIMEDOUT;
}

/* One block in or out of the FIFO. */
static int
transfer_block(struct sdmmc_cmd *cmd, uint32_t *buf)
{
	uint16_t want, got;
	uint32_t i;
	int r;

	want = (cmd->data_dir == SDMMC_DATA_READ) ?
	    SDHC_BUFFER_READ_READY : SDHC_BUFFER_WRITE_READY;

	if ((r = wait_intr(want, DATA_TIMEOUT_US, &got)) != OK)
		return r;
	log_trace(&sdmmc_log, "buffer ready: nis %04x st %08x bc %04x\n",
	    got, rd32(SDHC_PRESENT_STATE), rd16(SDHC_BLOCK_COUNT));
	wr16(SDHC_NINTR_STATUS, want);

	if (cmd->data_dir == SDMMC_DATA_READ)
		for (i = 0; i < cmd->blocklen / 4; i++)
			buf[i] = rd32(SDHC_DATA);
	else
		for (i = 0; i < cmd->blocklen / 4; i++)
			wr32(SDHC_DATA, buf[i]);

	return OK;
}

static int
sdhci_command(struct sdmmc_cmd *cmd)
{
	uint32_t mask, i;
	uint16_t mode, creg, got;
	int r;

	if (regs == 0)
		return ENXIO;

	/*
	 * The command line has to be free, and the data line too whenever
	 * this command will use it or will leave the card busy on it.
	 */
	mask = SDHC_CMD_INHIBIT_CMD;
	if (cmd->data_dir != SDMMC_DATA_NONE || cmd->rsp_type == SDMMC_RSP_R1B)
		mask |= SDHC_CMD_INHIBIT_DAT;
	if ((r = wait_state_clear(mask, CMD_TIMEOUT_US)) != OK) {
		log_warn(&sdmmc_log, "CMD%u: controller still busy (0x%08x)\n",
		    cmd->index, rd32(SDHC_PRESENT_STATE));
		return r;
	}

	/*
	 * Nothing left over from the command before - and it is worth
	 * checking that the acknowledgement took. A status bit that cannot
	 * be cleared makes the very next wait return at once on the previous
	 * command's completion, and then the response registers are read
	 * before there is a response in them. That failure reads as a card
	 * that answers nonsense, which is a long way from where it starts.
	 */
	wr16(SDHC_NINTR_STATUS, 0xffff);
	wr16(SDHC_EINTR_STATUS, 0xffff);
	if ((rd16(SDHC_NINTR_STATUS) & ~SDHC_CARD_INTERRUPT) != 0 ||
	    rd16(SDHC_EINTR_STATUS) != 0)
		log_debug(&sdmmc_log, "CMD%u: status will not clear "
		    "(nis %04x eis %04x)\n", cmd->index,
		    rd16(SDHC_NINTR_STATUS), rd16(SDHC_EINTR_STATUS));

	mode = 0;
	if (cmd->data_dir != SDMMC_DATA_NONE) {
		if (cmd->blocklen % 4 != 0 || ((uintptr_t)cmd->data & 3) != 0)
			return EINVAL;
		wr16(SDHC_BLOCK_SIZE, (uint16_t)cmd->blocklen);
		wr16(SDHC_BLOCK_COUNT, (uint16_t)cmd->blocks);
		log_trace(&sdmmc_log, "set bs %04x bc %04x, read back "
		    "%04x %04x\n", cmd->blocklen, cmd->blocks,
		    rd16(SDHC_BLOCK_SIZE), rd16(SDHC_BLOCK_COUNT));
		if (cmd->data_dir == SDMMC_DATA_READ)
			mode |= SDHC_READ_MODE;
		if (cmd->blocks > 1)
			mode |= SDHC_MULTI_BLOCK_MODE | SDHC_BLOCK_COUNT_ENABLE;
		if (cmd->stop)
			mode |= SDHC_AUTO_CMD12_ENABLE;
		/*
		 * Written only when there is data: the standard says the
		 * transfer mode register is not to be written otherwise, and
		 * with Data Present Select clear in the command the part
		 * ignores whatever it holds anyway.
		 */
		wr16(SDHC_TRANSFER_MODE, mode);
	}

	creg = (uint16_t)((cmd->index & SDHC_COMMAND_INDEX_MASK) <<
	    SDHC_COMMAND_INDEX_SHIFT);
	switch (cmd->rsp_type) {
	case SDMMC_RSP_NONE:
		creg |= SDHC_NO_RESPONSE;
		break;
	case SDMMC_RSP_R2:
		creg |= SDHC_RESP_LEN_136;
		if (check_responses)
			creg |= SDHC_CRC_CHECK_ENABLE;
		break;
	case SDMMC_RSP_R3:
		/* The OCR carries neither a CRC nor its own index. */
		creg |= SDHC_RESP_LEN_48;
		break;
	case SDMMC_RSP_R1B:
		creg |= SDHC_RESP_LEN_48_CHK_BUSY;
		if (check_responses)
			creg |= SDHC_CRC_CHECK_ENABLE |
			    SDHC_INDEX_CHECK_ENABLE;
		break;
	default:
		creg |= SDHC_RESP_LEN_48;
		if (check_responses)
			creg |= SDHC_CRC_CHECK_ENABLE |
			    SDHC_INDEX_CHECK_ENABLE;
		break;
	}
	if (cmd->data_dir != SDMMC_DATA_NONE)
		creg |= SDHC_DATA_PRESENT_SELECT;

	wr32(SDHC_ARGUMENT, cmd->arg);
	log_debug(&sdmmc_log, "CMD%u: creg %04x mode %04x st %08x\n",
	    cmd->index, creg, mode, rd32(SDHC_PRESENT_STATE));
	wr16(SDHC_COMMAND, creg);

	if ((r = wait_intr(SDHC_COMMAND_COMPLETE, CMD_TIMEOUT_US, &got)) != OK) {
		if (r == EIO)
			return command_error(cmd);
		log_warn(&sdmmc_log, "CMD%u: no completion\n", cmd->index);
		dump_regs("stuck");
		(void)reset(SDHC_RESET_CMD);
		(void)reset(SDHC_RESET_DAT);
		return r;
	}
	wr16(SDHC_NINTR_STATUS, SDHC_COMMAND_COMPLETE);

	/*
	 * A 136-bit response arrives without its CRC7 and stop bit, so the
	 * registers hold bits 127 to 8 of it right-shifted by eight. Shift
	 * it back so that resp[] is numbered the way the specification
	 * numbers the CSD and the CID, which is what the accessors in
	 * sdmmcreg.h assume; the byte that comes back in at the bottom is
	 * the CRC's place and reads as zero.
	 */
	if (cmd->rsp_type == SDMMC_RSP_R2) {
		uint32_t raw[4];

		for (i = 0; i < 4; i++)
			raw[i] = rd32(SDHC_RESPONSE + 4 * i);
		log_debug(&sdmmc_log, "CMD%u: raw %08x %08x %08x %08x "
		    "(nis %04x)\n", cmd->index, raw[3], raw[2], raw[1], raw[0],
		    got);
		for (i = 0; i < 4; i++)
			cmd->resp[i] = (raw[i] << 8) |
			    ((i > 0) ? (raw[i - 1] >> 24) : 0);
	} else if (cmd->rsp_type != SDMMC_RSP_NONE) {
		cmd->resp[0] = rd32(SDHC_RESPONSE);
		log_debug(&sdmmc_log, "CMD%u: raw %08x (nis %04x)\n",
		    cmd->index, cmd->resp[0], got);
	}

	if (cmd->data_dir != SDMMC_DATA_NONE) {
		uint8_t *p = cmd->data;

		for (i = 0; i < cmd->blocks; i++) {
			r = transfer_block(cmd, (uint32_t *)(void *)p);
			if (r != OK) {
				log_warn(&sdmmc_log, "CMD%u: block %u of %u "
				    "failed (%d), st %08x bs %04x bc %04x\n",
				    cmd->index, i, cmd->blocks, r,
				    rd32(SDHC_PRESENT_STATE),
				    rd16(SDHC_BLOCK_SIZE),
				    rd16(SDHC_BLOCK_COUNT));
				if (r == EIO)
					return command_error(cmd);
				log_warn(&sdmmc_log,
				    "CMD%u: block %u of %u stalled\n",
				    cmd->index, i, cmd->blocks);
				(void)reset(SDHC_RESET_CMD);
				(void)reset(SDHC_RESET_DAT);
				return r;
			}
			p += cmd->blocklen;
		}
	}

	/* A data transfer ends when the part says transfer complete. */
	if (cmd->data_dir != SDMMC_DATA_NONE) {
		r = wait_intr(SDHC_TRANSFER_COMPLETE, DATA_TIMEOUT_US, &got);
		if (r == EIO)
			return command_error(cmd);
		if (r != OK) {
			log_warn(&sdmmc_log, "CMD%u: transfer never "
			    "completed\n", cmd->index);
			(void)reset(SDHC_RESET_CMD);
			(void)reset(SDHC_RESET_DAT);
			return r;
		}
		wr16(SDHC_NINTR_STATUS, SDHC_TRANSFER_COMPLETE);
	} else if (cmd->rsp_type == SDMMC_RSP_R1B) {
		r = wait_busy_end(BUSY_TIMEOUT_US);
		if (r == EIO)
			return command_error(cmd);
		if (r != OK) {
			log_warn(&sdmmc_log, "CMD%u: the card stayed busy\n",
			    cmd->index);
			(void)reset(SDHC_RESET_CMD);
			(void)reset(SDHC_RESET_DAT);
			return r;
		}
	}

	return OK;
}

/*
 * Bring the controller up: reset it, give the socket power, and leave it at
 * the slowest clock with a one-bit bus, which is where a card expects to be
 * spoken to first.
 */
static int
sdhci_init(void)
{
	uint32_t caps, mhz;
	uint8_t pwr;
	void *v;
	int r;

	if (regs == 0) {
		v = vm_map_phys(SELF, (void *)reg_base, reg_size);
		if (v == MAP_FAILED) {
			log_warn(&sdmmc_log, "cannot map registers at 0x%lx; "
			    "not granted by RS?\n", (unsigned long)reg_base);
			return EPERM;
		}
		regs = (vir_bytes)v;
	}

	if (rst_regs == 0 && rst_base != 0 && rst_n > 0) {
		v = vm_map_phys(SELF, (void *)rst_base, rst_size);
		if (v == MAP_FAILED) {
			log_warn(&sdmmc_log, "cannot map the reset "
			    "controller at 0x%lx; not granted by RS? "
			    "carrying on without a hardware reset\n",
			    (unsigned long)rst_base);
			rst_n = 0;
		} else {
			rst_regs = (vir_bytes)v;
			log_debug(&sdmmc_log, "reset controller at 0x%lx\n",
			    (unsigned long)rst_base);
		}
	}

	if ((r = reset(SDHC_RESET_ALL)) != OK)
		return r;

	/*
	 * The Rockchip part checks the CMD line against what it drove and
	 * calls a mismatch an error; on this silicon it fires when there is
	 * no conflict, so Linux clears the check on every rk35xx before
	 * using the controller. The vendor block is wherever the part says
	 * it is, which is why the pointer is read rather than assumed.
	 */
	if (is_dwcmshc) {
		vendor_area = rd16(DWCMSHC_VENDOR_PTR) & DWCMSHC_VENDOR_PTR_MASK;
		if (vendor_area != 0 && vendor_area + 0x40 <= reg_size) {
			uint16_t ec;

			wr32(vendor_area + DWCMSHC_HOST_CTRL3, 0);
			/*
			 * And tell the part what is attached to it. Linux
			 * sets this bit only on the way into HS400, where it
			 * gates the data strobe, but it is named for the
			 * device and not for the mode, and the device here
			 * is soldered on and never anything else.
			 */
			ec = rd16(vendor_area + DWCMSHC_EMMC_CONTROL);
			wr16(vendor_area + DWCMSHC_EMMC_CONTROL,
			    ec | DWCMSHC_CARD_IS_EMMC);
			log_debug(&sdmmc_log, "vendor area at 0x%x, "
			    "emmc control %04x -> %04x\n", vendor_area, ec,
			    rd16(vendor_area + DWCMSHC_EMMC_CONTROL));
		} else {
			log_warn(&sdmmc_log, "vendor pointer 0x%x is out of "
			    "range; leaving the vendor block alone\n",
			    vendor_area);
			vendor_area = 0;
		}
	}

	{
		long v = 1;

		(void)env_parse("checkresp", "d", 0, &v, 0, 1);
		check_responses = (int)v;
		if (!check_responses)
			log_warn(&sdmmc_log, "response CRC and index checks "
			    "are off by request\n");
	}

	spec_version = rd16(SDHC_HOST_CTL_VERSION) & SDHC_SPEC_VERS_MASK;
	caps = rd32(SDHC_CAPABILITIES);

	/*
	 * Where the divider's input comes from.
	 *
	 * The capabilities register is supposed to name it in MHz, and on
	 * this part it does not: Rockchip's controller leaves the field
	 * zero and Linux reads the rate out of the clock tree instead
	 * (SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN). There is no clock tree here,
	 * so the fallback is the tree's max-frequency - the highest rate the
	 * board says the bus can carry.
	 *
	 * That is a safe direction to be wrong in, and it is the only safe
	 * one. Over-estimating the base makes every computed divider larger
	 * than it needs to be, so the card runs slower than asked; under-
	 * estimating it would run the card faster than asked, which is not
	 * slow data but wrong data. On this board the true base is 198 MHz
	 * (cclk_emmc) against a max-frequency of 200 MHz, so the error is
	 * one percent in the safe direction.
	 */
	mhz = (caps >> SDHC_BASE_FREQ_SHIFT) &
	    ((spec_version >= SDHC_SPEC_VERS_300) ?
	    SDHC_BASE_FREQ_MASK_V3 : SDHC_BASE_FREQ_MASK);
	if (mhz != 0) {
		base_clock = mhz * 1000000;
	} else {
		base_clock = (base_clock != 0) ? base_clock : 50000000;
		log_info(&sdmmc_log, "the part does not say what its base "
		    "clock is; taking %u Hz from the device tree\n",
		    base_clock);
	}

	/*
	 * Power. Pick the highest voltage the part says it supports: an
	 * embedded eMMC has its supply wired up and this register only gates
	 * it, while a socket needs it turned on at all.
	 */
	if (caps & SDHC_VOLTAGE_SUPP_3_3V)
		pwr = SDHC_VOLTAGE_3_3V << SDHC_VOLTAGE_SHIFT;
	else if (caps & SDHC_VOLTAGE_SUPP_3_0V)
		pwr = SDHC_VOLTAGE_3_0V << SDHC_VOLTAGE_SHIFT;
	else
		pwr = SDHC_VOLTAGE_1_8V << SDHC_VOLTAGE_SHIFT;
	wr8(SDHC_POWER_CTL, pwr);
	wr8(SDHC_POWER_CTL, pwr | SDHC_BUS_POWER);

	/* The data timeout counter, at its longest. */
	wr8(SDHC_TIMEOUT_CTL, SDHC_TIMEOUT_MAX);

	/*
	 * Every status bit is latched so that polling can see it; none is
	 * signalled to the GIC, because nothing here waits on a message.
	 *
	 * All but one: the card interrupt is an SDIO signal, there is no
	 * SDIO card here, and enabling it made the bit latch on every
	 * command. Linux leaves it out unless SDIO is in use, and a status
	 * bit that is always set is at best noise in a dump.
	 */
	wr16(SDHC_NINTR_STATUS_EN,
	    SDHC_NINTR_SIGNAL_MASK & ~SDHC_CARD_INTERRUPT);
	wr16(SDHC_EINTR_STATUS_EN, 0x03ff);
	wr16(SDHC_NINTR_SIGNAL_EN, 0);
	wr16(SDHC_EINTR_SIGNAL_EN, 0);

	if ((r = sdhci_set_bus_width(1)) != OK)
		return r;
	(void)sdhci_set_timing(0);
	if ((r = sdhci_set_clock(SDMMC_IDENT_HZ, NULL)) != OK)
		return r;

	/*
	 * The version field is not a number to add one to: 0, 1 and 2 are
	 * 1.00, 2.00 and 3.00, and from 3 on it counts 4.00, 4.10, 4.20.
	 * The Rockchip part reports 5, which is 4.20 and not 6.00.
	 */
	{
		static const char *const vers[] = {
			"1.00", "2.00", "3.00", "4.00", "4.10", "4.20"
		};
		const char *v = (spec_version <
		    sizeof(vers) / sizeof(vers[0])) ? vers[spec_version] : "?";

		log_info(&sdmmc_log, "SDHCI %s at 0x%lx, base clock %u Hz, "
		    "caps 0x%08x\n", v, (unsigned long)reg_base, base_clock,
		    caps);
	}
	dump_regs("after init");
	return OK;
}

static void
sdhci_exit(void)
{
	if (regs != 0) {
		(void)sdhci_set_clock(0, NULL);
		vm_unmap_phys(SELF, (void *)regs, reg_size);
	}
	regs = 0;
}

/*
 * Called for a node whose "compatible" names a part this file drives.
 * Records where the registers are; the mapping waits for init, exactly as
 * pl031_probe does in readclock.
 */
int
sdhci_probe(const struct fdt_node *node, const struct sdmmc_devinfo *info,
	struct sdmmc_host *host)
{
	if (info->size < 0x100)
		return ENXIO;

	reg_base = info->base;
	reg_size = info->size;
	base_clock = info->max_freq;	/* the fallback, see sdhci_init */

	rst_base = info->reset_base;
	rst_size = info->reset_size;
	rst_n = info->nresets;
	if (rst_n > SDMMC_MAX_RESETS)
		rst_n = SDMMC_MAX_RESETS;
	memcpy(rst_id, info->reset_id, sizeof(rst_id));
	is_dwcmshc = fdt_node_is_compatible(node, "rockchip,dwcmshc-sdhci") ||
	    fdt_node_is_compatible(node, "rockchip,rk3568-dwcmshc");

	host->init = sdhci_init;
	host->exit = sdhci_exit;
	host->set_clock = sdhci_set_clock;
	host->set_bus_width = sdhci_set_bus_width;
	host->set_timing = sdhci_set_timing;
	host->command = sdhci_command;
	host->max_bus_width = info->bus_width;
	host->max_freq = info->max_freq;
	/*
	 * Measured, not read out of the part: four blocks of 512 bytes go
	 * through and eight do not. See the comment on max_blocks.
	 */
	host->max_blocks = 4;
	return OK;
}
