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
 * Data moves by ADMA2 where the controller says it can, and through the
 * FIFO by the processor where it cannot.
 *
 * The first version of this driver did only the second, and said why: a DMA
 * engine that is not coherent with the caches needs its buffer cleaned
 * before the device reads it and invalidated after the device writes it, and
 * at the time this port had no way for a driver to ask for either. It has
 * had one since sys_cachectl(2) (port/PORTING-LOG.md, "Обслуживание кэшей
 * для DMA"), so that reason is gone and this comment is rewritten with the
 * code rather than left to mislead the next reader. The other half of the
 * old argument still holds and is why the maintenance is explicit rather
 * than avoided: a non-cacheable mapping of the buffer would not work here,
 * because the kernel maps all of RAM cacheably in its linear map and
 * sys_safecopy reads the buffer through it, and two mappings of one page
 * with different memory types are undefined.
 *
 * Which engine is decided by the capabilities register, not in advance:
 * ADMA2 when the part offers it and addresses fit in 32 bits, and the
 * programmed path otherwise. SDMA is offered by this part too and is not
 * used - it would be a second, untested way to do what ADMA2 already does,
 * and untested code that looks like working code is worse here than no code.
 *
 * Waiting is by interrupt where the tree gave the controller a line and RS
 * granted it, and by polling with a deadline where it did not - and also
 * where the line stops answering, which is checked rather than assumed. On
 * this board the four registers that do not do what the specification says
 * are documented in the log; an interrupt that does not arrive would be the
 * fifth, and the driver survives it instead of hanging.
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
 * The controller's reset lines and its core clock, as the tree names them.
 * What holds them, and what its registers look like, is sdmmc_clk.c's
 * business; on a machine whose tree names none, or names a holder that file
 * does not know, every operation on them answers ENOSYS and the firmware's
 * settings stand.
 */
static unsigned rst_n;
static struct sdmmc_line rst[SDMMC_MAX_RESETS];
static struct sdmmc_line clk_core;

/*
 * Whether the card clock's source follows the card clock - and getting that
 * wrong on the part that needs it is not a slow card but a wrong one.
 *
 * A standard part divides its base clock and touches nothing outside itself.
 * The Rockchip part cannot be driven that way below 52 MHz: with its delay
 * line bypassed it samples what the card drives against the *source* clock,
 * not against the divided card clock. Leave the source at 198 MHz and divide
 * it by 534 for identification and the card is clocked correctly while its
 * answers are sampled at the wrong instants: a 48-bit response comes back
 * with bits missing but still looking like a plausible number, and a 136-bit
 * one does not come back at all. That is how this driver spent an afternoon
 * believing an eMMC had the operating range 0x003f8000 when it was really
 * answering 0x40ff8080. Linux does the same thing for the same part
 * (dwcmshc_rk3568_set_clock): it sets the core clock to the card clock and
 * leaves the divider at one.
 *
 * So it is a property of the part, not of what the capabilities register
 * says - and the register is no guide here anyway: this part names 200 MHz
 * there whatever its source is set to, because the field is a constant of
 * the design rather than a reading of the clock. Deciding by "the register
 * names no base clock" was tried, and on the board it identified the eMMC at
 * 200 MHz / 534 and read CMD2 as an idle bus, exactly as above.
 */
static int source_follows;

/* Read out of the part at init, not written down here. */
static uint32_t base_clock;		/* the divider's input, in Hz */
static unsigned spec_version;
static int is_dwcmshc;			/* the Rockchip part: vendor area */
static unsigned vendor_area;

/* The host this file drives, kept so that init can revise what probe set. */
static struct sdmmc_host *this_host;

/*
 * A Qualcomm sdhci-msm (v5: "qcom,sdhci-msm-v5"), the Mi Pad 4's eMMC, is
 * taken over as the boot loader left it and never set up again (see
 * port/clover/hw-sdhci-msm.md and msm_takeover()).  Its power, voltage and
 * reset requests raise an interrupt that has to be answered, its card clock
 * is the GCC's and not the divider's, and its DLL holds a calibration this
 * driver could not redo: so no RESET_ALL, no POWER_CONTROL, no voltage
 * switch, no clock change, ever.  The registers it has beyond the standard
 * are inside the same block (v5), and the command queueing engine's are in
 * a second one, "cqhci".
 */
static int is_msm;
static int msm_cdr;		/* toggle the DLL's CDR around every command */
static phys_bytes cq_base;
static size_t cq_size;
static vir_bytes cq_regs;
static struct sdmmc_line msm_iface, msm_core;	/* its two GCC branches */
static int msm_alive;		/* both seen running: registers may be read */

#define MSM_DLL_CONFIG		0x200
#define MSM_DLL_STATUS		0x208
#define MSM_VENDOR_SPEC		0x20c
#define MSM_DDR_200_CFG		0x224
#define MSM_PWRCTL_STATUS	0x240
#define MSM_PWRCTL_CLEAR	0x248
#define MSM_PWRCTL_CTL		0x24c
#define MSM_MCI_VERSION		0x318
#define MSM_CDR_EN		(1u << 17)
#define MSM_CDR_EXT_EN		(1u << 19)
#define MSM_DLL_LOCK		(1u << 7)
#define MSM_DDR_DLL_LOCK	(1u << 11)
#define MSM_ENH_STROBE		(1u << 1)	/* DDR_200_CFG CMDIN_RCLK_EN */
#define MSM_PWR_BUS_SUCCESS	(1u << 0)
#define MSM_PWR_IO_SUCCESS	(1u << 2)
#define CQ_CFG			0x08
#define CQ_CFG_ENABLE		(1u << 0)
#define CQ_CRYPTO_PARM		0x70
#define CQ_CRYPTO_CE		(1u << 8)
#define HC2_V4_ENABLE		(1u << 12)
#define HC2_64BIT_ADDR		(1u << 13)

/*
 * The interrupt.
 *
 * irq_line is what the tree says; irq_ok is whether RS actually granted it
 * and the hook was set. irq_dead is set the first time the line misbehaves,
 * and from then on every wait is a poll: on this board the specification has
 * already been wrong about four registers, and a driver that hangs forever
 * because the fifth one is an interrupt that never comes is a worse outcome
 * than a driver that goes slowly.
 */
static int irq_line = -1;
static int irq_hook;
static int irq_ok;
static int irq_dead;

/*
 * How many hardware interrupts may arrive without the status register saying
 * anything new before the line is declared useless.
 *
 * The failure this guards against is not hypothetical in shape: the line is
 * asserted while a status bit is set, so a bit that is signalled and never
 * cleared makes every sys_irqenable() produce another interrupt at once.
 * That is a livelock, not a hang, and it would be visible only as a system
 * that stopped doing anything else - which is exactly the shape of the UART
 * interrupt storm this port has already met once (see the log, "Молчание
 * после баннера").
 */
#define IRQ_SPURIOUS_MAX	64

/*
 * DMA.
 *
 * dma_mode is one of SDHC_DMA_SELECT_* or -1 for "the processor moves it".
 * The descriptor table is one page, allocated once: at eight bytes and up
 * to 64 KiB of payload per descriptor, a page describes 32 MiB, which is
 * three orders of magnitude more than one command of this driver carries.
 */
#define ADMA_TABLE_BYTES	4096
#define ADMA_DESCS		(ADMA_TABLE_BYTES / 8)

static int dma_mode = -1;
static uint32_t *adma_desc;
static phys_bytes adma_desc_phys;

/*
 * Cache maintenance, with the answer looked at.
 *
 * Taken from dwmac_ring.c, and for the reason written there: a call that
 * quietly does nothing looks exactly like a call that worked, and what it
 * leaves behind is not an error but wrong data three layers away. Reported
 * once, because a driver that prints on every block is a driver nobody
 * reads - but reported, and the transfer is refused rather than run blind.
 */
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
 * Has one of the wanted normal-interrupt bits arrived, or an error?
 *
 * The error is looked at first on purpose: a transfer that ends badly can
 * raise the bit the caller is waiting for in the same status word, and
 * taking that as success would turn a CRC error into data.
 *
 * The bit is not cleared here: which of several bits arrived is the caller's
 * business, and a data transfer wants to clear buffer-ready without touching
 * transfer-complete.
 */
static int
check_intr(uint16_t wanted, uint16_t *got)
{
	uint16_t n = rd16(SDHC_NINTR_STATUS);

	if (n & SDHC_ERROR_INTERRUPT)
		return EIO;
	if (n & wanted) {
		if (got != NULL)
			*got = n;
		return OK;
	}
	return EAGAIN;
}

/* Spin until one of them arrives, or the time runs out. */
static int
poll_intr(uint16_t wanted, uint32_t usecs, uint16_t *got)
{
	spin_t s;
	int r;

	spin_init(&s, usecs);
	do {
		if ((r = check_intr(wanted, got)) != EAGAIN)
			return r;
	} while (spin_check(&s));

	return ETIMEDOUT;
}

/*
 * Sleep until one of them arrives, or the alarm says the time is up.
 *
 * Returns EAGAIN, and only EAGAIN, when the interrupt line has proved
 * useless; the caller then polls, and every later wait polls too. Three
 * things count as useless and all three are checked rather than assumed:
 * the kernel refusing to arm the line, the line raising interrupt after
 * interrupt without the status register saying anything new, and the alarm
 * expiring while no interrupt at all has been seen.
 *
 * The last of those deserves the distinction it gets. If the deadline
 * passes and interrupts *were* arriving, the transfer really did fail and
 * ETIMEDOUT is the honest answer. If it passes with none seen, the failure
 * is more likely the line than the card, and it is worth retrying the same
 * wait by polling before telling the caller anything.
 *
 * Requests that arrive while we sleep are queued by libblockdriver, which
 * is the same thing emmc(8) does in this directory and for the same reason:
 * this driver's main loop is blockdriver_task(), and a message taken off
 * the queue here would otherwise be lost.
 */
static int
intr_wait(uint16_t wanted, uint32_t usecs, uint16_t *got)
{
	message m;
	unsigned spurious = 0;
	int ipc_status, r, seen = 0, expired = 0;

	sys_setalarm(micros_to_ticks(usecs), 0);

	for (;;) {
		if ((r = check_intr(wanted, got)) != EAGAIN)
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
			    "with nothing to show for it (nis %04x eis %04x); "
			    "polling from now on\n", irq_line, spurious,
			    rd16(SDHC_NINTR_STATUS), rd16(SDHC_EINTR_STATUS));
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
		/* Somebody else's request: it waits until this one is done. */
		blockdriver_mq_queue(&m, ipc_status);
	}

	sys_setalarm(0, 0);
	(void)sys_irqdisable(&irq_hook);

	if (r == EAGAIN)
		irq_dead = 1;
	return r;
}

/*
 * Wait for one of the wanted bits, by whichever means this machine has.
 *
 * A wait that the interrupt path gives up on is retried by polling rather
 * than reported: the deadline has passed by then, but the deadline was a
 * bound on the hardware and the hardware has not been given its chance yet.
 */
static int
wait_intr(uint16_t wanted, uint32_t usecs, uint16_t *got)
{
	int r;

	if (irq_ok && !irq_dead) {
		if ((r = intr_wait(wanted, usecs, got)) != EAGAIN)
			return r;
	}
	return poll_intr(wanted, usecs, got);
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
	sdmmc_reset_pulse(rst, rst_n);
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
	/*
	 * A full reset of a Qualcomm part is a power request that has to be
	 * answered, and drops what the boot loader set up; command and data
	 * resets raise no request and are what error recovery needs.
	 */
	if (is_msm && (mask & SDHC_RESET_ALL)) {
		log_warn(&sdmmc_log, "refusing a full reset of the taken-over "
		    "controller\n");
		return EPERM;
	}

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
 * divisor is 2N, and N == 0 means the base clock undivided. When the source
 * follows the card clock (source_follows above) the source is already at or
 * below the wish and N is zero; otherwise the smallest N that brings the
 * base down to it.
 */
static int
sdhci_set_clock(uint32_t hz, uint32_t *actual)
{
	uint32_t n, got, src;
	uint16_t clk;
	spin_t s;

	if (is_msm)		/* taken over: the bus stays as it was left */
		return EPERM;

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
	if (source_follows && sdmmc_clk_set_rate(&clk_core, hz, &src) == OK)
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

	if (is_msm)
		return EPERM;

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

	if (is_msm)
		return EPERM;

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

	/*
	 * The ADMA error state is named separately because it is the only
	 * error here that is about the driver rather than about the card: it
	 * says the engine choked on a descriptor this file wrote, and which
	 * state it was in says whether it was fetching the descriptor or
	 * acting on it.
	 */
	if (e & SDHC_ADMA_ERROR)
		log_warn(&sdmmc_log, "CMD%u: ADMA error, state 0x%08x, "
		    "table at 0x%08x\n", cmd->index,
		    rd32(SDHC_ADMA_ERROR_STATUS), rd32(SDHC_ADMA_ADDR));

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

	/*
	 * A poll, and deliberately not the interrupt path.
	 *
	 * Buffer-ready is a level, not an event: it says the FIFO has data
	 * or has room, and it stays set until this loop has emptied or
	 * filled it. Signalling that to the GIC is the shape that livelocks
	 * - every re-enable finds the line still asserted - and the wait is
	 * microseconds anyway, less than the message it would cost. So the
	 * programmed path stays what it always was; the interrupt is for
	 * the two events that really are events, and both of them belong to
	 * the transfer as a whole.
	 */
	if ((r = poll_intr(want, DATA_TIMEOUT_US, &got)) != OK)
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

/*
 * Describe the buffer to the controller, and hand the caches over.
 *
 * Answers OK when the transfer will be done by the engine and something
 * else when it will not; the caller then falls back to the FIFO, which is
 * why every refusal here is a refusal and not a failure. The cases that
 * refuse are the ones where a 32-bit ADMA2 descriptor cannot say what is
 * wanted: no physical address, a buffer that does not fit below 4 GiB, one
 * longer than the table can describe, one not aligned to a word.
 *
 * The cache maintenance is the rule taken from NetBSD's bus_dma and written
 * up in the log: clean before the device reads, invalidate before the device
 * writes - and, for a read, invalidate again afterwards, because a core that
 * speculates could have pulled the line back in while the transfer ran. The
 * buffer this driver hands down is page-aligned and a whole number of
 * sectors, so no cache line of it is shared with anything else and the
 * partial-line rule of sys_cachectl(2) never comes into it.
 */
static int
dma_setup(struct sdmmc_cmd *cmd)
{
	size_t total = (size_t)cmd->blocks * cmd->blocklen;
	phys_bytes addr = cmd->data_phys;
	size_t left = total;
	unsigned n = 0;
	uint8_t ctl;
	int op;

	if (dma_mode < 0 || adma_desc == NULL || cmd->data_phys == 0 ||
	    total == 0)
		return EINVAL;
	if ((uint64_t)addr + total > 0x100000000ULL || (addr & 3) != 0)
		return EINVAL;
	if ((total + ADMA2_MAX_LEN - 1) / ADMA2_MAX_LEN > ADMA_DESCS)
		return EINVAL;

	while (left > 0) {
		size_t len = (left > ADMA2_MAX_LEN) ? ADMA2_MAX_LEN : left;
		uint32_t attr = ADMA2_ATTR_VALID | ADMA2_ATTR_ACT_TRAN;

		if (len == left)
			attr |= ADMA2_ATTR_END;
		/*
		 * A length of 65536 is written as zero, which is what the
		 * mask does on its own; the port is little-endian only, so
		 * the two halves of the word are attribute then length
		 * exactly as the specification draws them.
		 */
		adma_desc[2 * n] = attr | ((uint32_t)(len & 0xffff) << 16);
		adma_desc[2 * n + 1] = (uint32_t)addr;
		addr += len;
		left -= len;
		n++;
	}

	op = (cmd->data_dir == SDMMC_DATA_WRITE) ?
	    CACHE_CLEAN : CACHE_INVALIDATE;
	if (cache_op(op, cmd->data, total) != OK)
		return EIO;
	if (cache_op(CACHE_CLEAN, adma_desc, n * 8) != OK)
		return EIO;

	ctl = rd8(SDHC_HOST_CTL) &
	    (uint8_t)~(SDHC_DMA_SELECT_MASK << SDHC_DMA_SELECT_SHIFT);
	wr8(SDHC_HOST_CTL,
	    ctl | (uint8_t)(dma_mode << SDHC_DMA_SELECT_SHIFT));
	wr32(SDHC_ADMA_ADDR, (uint32_t)adma_desc_phys);

	log_trace(&sdmmc_log, "adma: %u desc for %u bytes at phys 0x%lx, "
	    "table 0x%lx\n", n, (unsigned)total, (unsigned long)cmd->data_phys,
	    (unsigned long)adma_desc_phys);
	return OK;
}

static int
sdhci_command(struct sdmmc_cmd *cmd)
{
	uint32_t mask, i;
	uint16_t mode, creg, got;
	int r, use_dma = 0;

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
		 * And whether the controller fetches it or the processor
		 * feeds it. Asked per command rather than once, because the
		 * answer depends on the buffer: the card protocol's own
		 * short reads land wherever their caller had them.
		 */
		if (dma_setup(cmd) == OK) {
			use_dma = 1;
			mode |= SDHC_DMA_ENABLE;
		}
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

	/*
	 * A tuned Qualcomm part (HS200, or HS400 without the enhanced strobe)
	 * samples read data with its clock-data recovery on and everything
	 * else with it off: Linux flips the two bits on every command.
	 */
	if (msm_cdr) {
		uint32_t dll = rd32(MSM_DLL_CONFIG);

		if (cmd->data_dir == SDMMC_DATA_READ)
			dll = (dll | MSM_CDR_EN) & ~MSM_CDR_EXT_EN;
		else
			dll = (dll & ~MSM_CDR_EN) | MSM_CDR_EXT_EN;
		wr32(MSM_DLL_CONFIG, dll);
	}

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

	if (cmd->data_dir != SDMMC_DATA_NONE && !use_dma) {
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
		/*
		 * And once more, after the device is done: while the
		 * transfer ran, a speculating core may have pulled lines of
		 * the buffer back into the cache from memory the device had
		 * not written yet. NetBSD does the same and says why in
		 * bus_dma; on ARMv8 there is no core that does not
		 * speculate, so there is no condition around it.
		 */
		if (use_dma && cmd->data_dir == SDMMC_DATA_READ)
			(void)cache_op(CACHE_INVALIDATE, cmd->data,
			    (size_t)cmd->blocks * cmd->blocklen);
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
 * Take a Qualcomm sdhci-msm over as the boot loader left it (is_msm).  The
 * order is port/clover/hw-sdhci-msm.md's checklist: the block answers; a
 * power request left pending is answered; command queueing is off (or the
 * driver stops here) and inline encryption is off; the bus is read rather
 * than set - width, timing, the clock running, the DLL locked where the
 * timing needs it - and anything this driver cannot use as it stands is a
 * refusal.  The only writes are to the power acknowledgement, the crypto
 * parameter, and Host Control 2's version-4 bits (so that ADMA descriptors
 * have the 32-bit shape this driver writes), with its voltage bit kept.
 */
static int
msm_takeover(void)
{
	uint32_t mci, s, vs, dll, dlls, ddr200, st;
	uint16_t ver, hc2, clk;
	uint8_t hc1;
	unsigned i, uhs;
	int hs400, es, on_iface, on_core;
	void *v;

	/*
	 * Before the first read of the block: both of its clocks running, as
	 * the GCC itself reports it.  "Not known" is a refusal like "off" -
	 * a stalled bus is not a failure anything after it can report.
	 */
	on_iface = sdmmc_clk_is_on(&msm_iface);
	on_core = sdmmc_clk_is_on(&msm_core);
	if (on_iface != 1 || on_core != 1) {
		log_warn(&sdmmc_log, "msm: clocks iface %s, core %s; not "
		    "touching the controller\n",
		    on_iface == 1 ? "on" : on_iface == 0 ? "off" : "unknown",
		    on_core == 1 ? "on" : on_core == 0 ? "off" : "unknown");
		return ENXIO;
	}
	msm_alive = 1;

	ver = rd16(SDHC_HOST_CTL_VERSION);
	mci = rd32(MSM_MCI_VERSION);
	if (ver == 0xffff || mci == 0 || mci == 0xffffffff) {
		log_warn(&sdmmc_log, "msm: the controller does not answer "
		    "(version %04x, core %08x): clock off?\n", ver, mci);
		return ENXIO;
	}

	/* A power request nobody answered: answer it, success. */
	if ((s = rd32(MSM_PWRCTL_STATUS) & 0xf) != 0) {
		uint32_t ack = 0;

		for (i = 0; i < 10; i++) {
			wr32(MSM_PWRCTL_CLEAR, s);
			if ((rd32(MSM_PWRCTL_STATUS) & s) == 0)
				break;
			micro_delay(10);
		}
		if (s & 0x3)
			ack |= MSM_PWR_BUS_SUCCESS;
		if (s & 0xc)
			ack |= MSM_PWR_IO_SUCCESS;
		wr32(MSM_PWRCTL_CTL, ack);
		log_info(&sdmmc_log, "msm: answered a pending power request "
		    "0x%x\n", s);
	}

	/* Command queueing must be off; inline encryption off. */
	if (cq_size != 0 && cq_regs == 0) {
		if ((v = vm_map_phys(SELF, (void *)cq_base, cq_size)) ==
		    MAP_FAILED) {
			log_warn(&sdmmc_log, "msm: cannot map the cqhci block\n");
			return EPERM;
		}
		cq_regs = (vir_bytes)v;
	}
	if (cq_regs != 0) {
		if (*(volatile uint32_t *)(cq_regs + CQ_CFG) & CQ_CFG_ENABLE) {
			log_warn(&sdmmc_log, "msm: command queueing was left "
			    "on; not taking the controller over\n");
			return EIO;
		}
		if (*(volatile uint32_t *)(cq_regs + CQ_CRYPTO_PARM) &
		    CQ_CRYPTO_CE) {
			*(volatile uint32_t *)(cq_regs + CQ_CRYPTO_PARM) = 0;
			log_info(&sdmmc_log, "msm: inline encryption turned "
			    "off for plain requests\n");
		}
	}

	/* What the boot loader left. */
	hc1 = rd8(SDHC_HOST_CTL);
	hc2 = rd16(SDHC_HOST_CTL2);
	clk = rd16(SDHC_CLOCK_CTL);
	vs = rd32(MSM_VENDOR_SPEC);
	dll = rd32(MSM_DLL_CONFIG);
	dlls = rd32(MSM_DLL_STATUS);
	ddr200 = rd32(MSM_DDR_200_CFG);
	log_info(&sdmmc_log, "msm: core %u.%02x; host ctl %02x/%04x, clock "
	    "%04x, vendor %08x, dll %08x/%08x, ddr200 %08x\n", mci >> 28,
	    mci & 0xff, hc1, hc2, clk, vs, dll, dlls, ddr200);

	if ((clk & (SDHC_INTCLK_STABLE | SDHC_SDCLK_ENABLE)) !=
	    (SDHC_INTCLK_STABLE | SDHC_SDCLK_ENABLE)) {
		log_warn(&sdmmc_log, "msm: the card clock is not running\n");
		return EIO;
	}
	uhs = hc2 & 7;
	hs400 = ((vs >> 8) & 3) == 3;
	es = (ddr200 & MSM_ENH_STROBE) != 0;
	if (uhs >= 3 && !(dlls & MSM_DLL_LOCK)) {
		log_warn(&sdmmc_log, "msm: HS200/HS400 timing without a "
		    "locked DLL\n");
		return EIO;
	}
	if (hs400 && !(dlls & MSM_DDR_DLL_LOCK))
		log_warn(&sdmmc_log, "msm: HS400 with the DDR DLL not "
		    "reported locked; going on\n");
	msm_cdr = (uhs == 3 && !es);

	if (this_host != NULL) {
		this_host->adopt_width = (hc1 & SDHC_8BIT_MODE) ? 8 :
		    (hc1 & SDHC_4BIT_MODE) ? 4 : 1;
		if (hs400) {
			this_host->adopt_mode = es ? "HS400 enhanced strobe "
			    "(as left)" : "HS400 (as left)";
			this_host->adopt_hz = 200000000;
		} else if (uhs == 3) {
			this_host->adopt_mode = "HS200 (as left)";
			this_host->adopt_hz = 200000000;
		} else if (uhs == 4) {
			this_host->adopt_mode = "DDR52 (as left)";
			this_host->adopt_hz = 52000000;
		} else {
			this_host->adopt_mode = (hc1 & SDHC_HIGH_SPEED) ?
			    "high speed (as left)" : "legacy (as left)";
			this_host->adopt_hz = (hc1 & SDHC_HIGH_SPEED) ?
			    52000000 : 26000000;
		}
	}

	/* 32-bit ADMA2 descriptors: version 4 mode and 64-bit off. */
	if (hc2 & (HC2_V4_ENABLE | HC2_64BIT_ADDR)) {
		wr16(SDHC_HOST_CTL2, hc2 & ~(HC2_V4_ENABLE | HC2_64BIT_ADDR));
		log_info(&sdmmc_log, "msm: host version 4 mode off (%04x -> "
		    "%04x)\n", hc2, rd16(SDHC_HOST_CTL2));
	}

	/* The upper half of the ADMA address, which 64-bit mode used. */
	wr32(SDHC_ADMA_ADDR + 4, 0);

	/* Nothing in flight from the boot loader. */
	st = rd32(SDHC_PRESENT_STATE);
	if (st & (SDHC_CMD_INHIBIT_CMD | SDHC_CMD_INHIBIT_DAT)) {
		(void)reset(SDHC_RESET_CMD);
		(void)reset(SDHC_RESET_DAT);
	}
	wr16(SDHC_NINTR_STATUS, 0xffff);
	wr16(SDHC_EINTR_STATUS, 0xffff);
	return OK;
}

/*
 * Bring the controller up: reset it, give the socket power, and leave it at
 * the slowest clock with a one-bit bus, which is where a card expects to be
 * spoken to first.  A Qualcomm part is taken over instead (msm_takeover()).
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

	if (is_msm) {
		if ((r = msm_takeover()) != OK)
			return r;
	} else if ((r = reset(SDHC_RESET_ALL)) != OK)
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
	 * The capabilities register is supposed to name it in MHz. Some
	 * parts leave the field zero (Linux: SDHCI_QUIRK_CAP_CLOCK_BASE_
	 * BROKEN), and then the fallback is the tree's max-frequency - the
	 * highest rate the board says the bus can carry. That is a safe
	 * direction to be wrong in, and the only safe one: over-estimating
	 * the base makes every computed divider larger than it needs to be,
	 * so the card runs slower than asked; under-estimating it would run
	 * the card faster than asked, which is not slow data but wrong data.
	 *
	 * On a part whose source follows the card clock (source_follows) the
	 * number is only a starting point: the first set_clock sets the
	 * source and learns its real rate from sdmmc_clk.c.
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

	source_follows = 0;
	if (is_dwcmshc) {
		if (clk_core.ctl != SDMMC_NO_LINE)
			source_follows = 1;
		else
			log_warn(&sdmmc_log, "this part needs its core clock "
			    "moved with the card clock, and the tree names "
			    "none this driver can set; identification will "
			    "probably fail\n");
	}

	/*
	 * Which engine moves the data, decided by what the part says rather
	 * than by what this board is known to have.
	 *
	 * ADMA2 with 32-bit descriptors, or nothing. SDMA is refused even
	 * where the capability bit is set: it would be a second way to do
	 * what the first already does, tested nowhere, and it needs the
	 * driver to reprogram the address at every buffer boundary - a
	 * mechanism with no counterpart in ADMA2 and nothing here to
	 * exercise it. A part that wants 96-bit descriptors (the 64-bit
	 * system bus of specification 3.00) is refused for the same reason:
	 * a descriptor of the wrong shape does not fail, it transfers into
	 * the wrong address.
	 */
	if (adma_desc == NULL)
		adma_desc = alloc_contig(ADMA_TABLE_BYTES, AC_ALIGN4K,
		    &adma_desc_phys);

	dma_mode = -1;
	if (!(caps & SDHC_ADMA2_SUPPORT))
		log_info(&sdmmc_log, "no ADMA2 in the capabilities; the "
		    "processor will move the data\n");
	else if ((caps & SDHC_64BIT_BUS_V3) && !is_msm)
		log_info(&sdmmc_log, "the part wants 64-bit descriptors, "
		    "which this driver does not write; the processor will "
		    "move the data\n");
	else if (adma_desc == NULL || adma_desc_phys == 0 ||
	    adma_desc_phys + ADMA_TABLE_BYTES > 0x100000000ULL)
		log_warn(&sdmmc_log, "no descriptor table below 4 GiB; the "
		    "processor will move the data\n");
	else
		dma_mode = SDHC_DMA_SELECT_ADMA2_32;

	/*
	 * And the ceiling on one command.
	 *
	 * Four blocks is the FIFO's, measured on this board; it has nothing
	 * to do with the engine, which streams the bytes as fast as it takes
	 * them off the bus. What bounds a DMA transfer is the block count
	 * register, which is sixteen bits, and the descriptor table, which
	 * dma_setup() checks per command.
	 */
	if (this_host != NULL) {
		this_host->max_blocks_pio = 4;
		this_host->max_blocks = (dma_mode >= 0) ?
		    0xffff : this_host->max_blocks_pio;
	}

	/*
	 * Power. Pick the highest voltage the part says it supports: an
	 * embedded eMMC has its supply wired up and this register only gates
	 * it, while a socket needs it turned on at all.
	 */
	if (!is_msm) {		/* a Qualcomm part keeps what it was given */
		if (caps & SDHC_VOLTAGE_SUPP_3_3V)
			pwr = SDHC_VOLTAGE_3_3V << SDHC_VOLTAGE_SHIFT;
		else if (caps & SDHC_VOLTAGE_SUPP_3_0V)
			pwr = SDHC_VOLTAGE_3_0V << SDHC_VOLTAGE_SHIFT;
		else
			pwr = SDHC_VOLTAGE_1_8V << SDHC_VOLTAGE_SHIFT;
		wr8(SDHC_POWER_CTL, pwr);
		wr8(SDHC_POWER_CTL, pwr | SDHC_BUS_POWER);
	}

	/* The data timeout counter, at its longest. */
	wr8(SDHC_TIMEOUT_CTL, SDHC_TIMEOUT_MAX);

	/*
	 * The interrupt line, if the tree named one and RS granted it.
	 *
	 * A refusal is not fatal and not even a warning at the level that
	 * stops a boot: waiting by polling is what this driver did until
	 * now, and it works. It is said out loud, though, because "slow for
	 * a reason nobody noticed" is the failure this note exists to
	 * prevent.
	 */
	if (irq_line >= 0 && !irq_ok) {
		/*
		 * The hook id is the bit the kernel sets in the notification,
		 * so it has to fit irq_id_t (64 bits here): the first version
		 * passed the line number, which worked for the eMMC's 51 and
		 * was refused with EINVAL for the SD socket's 130. One line
		 * per driver, so any small constant does; 1, as xhci uses.
		 */
		irq_hook = SDMMC_IRQ_HOOK;
		if ((r = sys_irqsetpolicy(irq_line, 0, &irq_hook)) != OK)
			log_warn(&sdmmc_log, "line %d not granted (%d); every "
			    "wait will be a poll\n", irq_line, r);
		else
			irq_ok = 1;
	} else if (irq_line < 0) {
		log_info(&sdmmc_log, "the tree names no interrupt for this "
		    "controller; every wait will be a poll\n");
	}

	/*
	 * Every status bit is latched so that a poll can see it.
	 *
	 * All but one: the card interrupt is an SDIO signal, there is no
	 * SDIO card here, and enabling it made the bit latch on every
	 * command. Linux leaves it out unless SDIO is in use, and a status
	 * bit that is always set is at best noise in a dump.
	 */
	wr16(SDHC_NINTR_STATUS_EN,
	    SDHC_NINTR_SIGNAL_MASK & ~SDHC_CARD_INTERRUPT);
	wr16(SDHC_EINTR_STATUS_EN, 0x03ff);

	/*
	 * Signalled to the GIC: only the two events a wait ever sleeps on,
	 * plus every error.
	 *
	 * The narrowness is the point. The line is asserted for as long as a
	 * signalled status bit is set, so a bit that is signalled and left
	 * standing turns each sys_irqenable() into another interrupt at
	 * once - a livelock rather than a hang, and one this port has
	 * already met on the UART. Buffer-ready is deliberately not
	 * signalled: it belongs to the programmed path, which stays a poll,
	 * and it is set for as long as the FIFO has room.
	 */
	if (irq_ok) {
		wr16(SDHC_NINTR_SIGNAL_EN,
		    SDHC_COMMAND_COMPLETE | SDHC_TRANSFER_COMPLETE);
		wr16(SDHC_EINTR_SIGNAL_EN, 0x03ff);
	} else {
		wr16(SDHC_NINTR_SIGNAL_EN, 0);
		wr16(SDHC_EINTR_SIGNAL_EN, 0);
	}

	if (!is_msm) {
		if ((r = sdhci_set_bus_width(1)) != OK)
			return r;
		(void)sdhci_set_timing(0);
		if ((r = sdhci_set_clock(SDMMC_IDENT_HZ, NULL)) != OK)
			return r;
	}

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
		log_info(&sdmmc_log, "transfers by %s, waits by %s\n",
		    (dma_mode >= 0) ? "ADMA2" : "the processor",
		    irq_ok ? "interrupt" : "polling");
	}
	dump_regs("after init");
	return OK;
}

static void
sdhci_exit(void)
{
	if (regs != 0 && (!is_msm || msm_alive)) {
		/*
		 * The signals go before the line does: a bit left signalled
		 * on a line nobody is listening to is the same livelock as
		 * before, only now with no driver to notice it.
		 */
		wr16(SDHC_NINTR_SIGNAL_EN, 0);
		wr16(SDHC_EINTR_SIGNAL_EN, 0);
		/*
		 * Not the clock of a taken-over part: stopping it is a
		 * change this driver could not undo, and the next owner -
		 * a restarted instance - takes the part over as it finds it.
		 */
		if (!is_msm)
			(void)sdhci_set_clock(0, NULL);
	}
	/* A block whose clocks were off is unmapped untouched. */
	if (regs != 0)
		vm_unmap_phys(SELF, (void *)regs, reg_size);
	if (cq_regs != 0)
		vm_unmap_phys(SELF, (void *)cq_regs, cq_size);
	if (irq_ok) {
		(void)sys_irqrmpolicy(&irq_hook);
		irq_ok = 0;
	}
	regs = 0;
	cq_regs = 0;
	msm_alive = 0;
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
	irq_line = info->irq;
	this_host = host;

	rst_n = info->nresets;
	if (rst_n > SDMMC_MAX_RESETS)
		rst_n = SDMMC_MAX_RESETS;
	memcpy(rst, info->reset, sizeof(rst));
	/*
	 * "core" is the binding's name for the clock the card clock is
	 * divided from, in the generic SDHCI binding and in Rockchip's.
	 */
	if (sdmmc_clk_get(node, "core", &clk_core) != OK)
		clk_core.ctl = SDMMC_NO_LINE;
	is_dwcmshc =fdt_node_is_compatible(node, "rockchip,dwcmshc-sdhci") ||
	    fdt_node_is_compatible(node, "rockchip,rk3568-dwcmshc");

	/*
	 * A Qualcomm part: taken over, never set up (msm_takeover()), and a
	 * phone's eMMC - the boot loaders live on it - so written only
	 * through a gptpartN= window (sdmmc_gpt.c).  Its "cqhci" block is
	 * found by name; the tree lists hc, cqhci, ice.
	 */
	is_msm = fdt_node_is_compatible(node, "qcom,sdhci-msm-v5");
	if (is_msm) {
		const char *names, *p;
		unsigned len, idx;
		u64_t a, sz;

		/*
		 * Only what the boot loader itself used: the soldered eMMC
		 * it read this kernel from.  A socket it may never have
		 * clocked, and on this SoC a read of an unclocked block does
		 * not return ones, it stalls the bus - so a socket is not
		 * taken over at all, and the eMMC only with both of its
		 * clocks seen running (msm_takeover()).
		 */
		if (!info->non_removable) {
			log_info(&sdmmc_log, "a Qualcomm socket is not taken "
			    "over: nothing says the boot loader clocked it\n");
			return ENXIO;
		}
		if (sdmmc_clk_get(node, "iface", &msm_iface) != OK)
			msm_iface.ctl = SDMMC_NO_LINE;
		if (sdmmc_clk_get(node, "core", &msm_core) != OK)
			msm_core.ctl = SDMMC_NO_LINE;
		/* Never pulled: the reset lines are the GCC's block reset. */
		rst_n = 0;

		cq_base = 0;
		cq_size = 0;
		if ((names = fdt_getprop(node, "reg-names", &len)) != NULL)
			for (p = names, idx = 0; p < names + len;
			    p += strlen(p) + 1, idx++)
				if (strcmp(p, "cqhci") == 0 &&
				    fdt_node_reg(node, idx, &a, &sz) == 0) {
					cq_base = (phys_bytes)a;
					cq_size = (size_t)sz;
				}
		host->protect = 1;
		host->adopt = 1;
	}

	host->init = sdhci_init;
	host->exit = sdhci_exit;
	host->set_clock = sdhci_set_clock;
	host->set_bus_width = sdhci_set_bus_width;
	host->set_timing = sdhci_set_timing;
	host->command = sdhci_command;
	host->max_bus_width = info->bus_width;
	host->max_freq = info->max_freq;
	/*
	 * Measured, not read out of the part: through the FIFO, four blocks
	 * of 512 bytes go through and eight do not. See the comment on
	 * max_blocks. Both start at the FIFO's ceiling because probe has not
	 * touched the hardware yet and does not know what it can do; init
	 * raises max_blocks once the capabilities register has answered.
	 */
	host->max_blocks = 4;
	host->max_blocks_pio = 4;
	return OK;
}
