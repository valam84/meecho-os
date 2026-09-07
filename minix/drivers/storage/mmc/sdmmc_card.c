/*
 * The card, above whichever host controller found it.
 *
 * Everything here is the SD Physical Layer and JEDEC eMMC specifications and
 * nothing here is a register: the file talks in CMD numbers and gets them
 * executed by host->command(). That is the whole point of the split - the
 * board has two controllers with nothing in common at the register level and
 * the same cards behind them.
 *
 * The order of identification is the one every bootloader uses: CMD0 to put
 * the card in idle, CMD8 to ask an SD card what it is, ACMD41 to bring an SD
 * card up, and CMD1 for an eMMC when that fails. Doing it the other way
 * round is tempting when the target is an eMMC, but CMD1 is reserved on SD
 * and some cards answer it anyway, so the standard order is also the one
 * that cannot mis-identify.
 *
 * Speed. The card is taken to plain high speed - 52 MHz for an eMMC, the
 * card's default for SD - and no further. HS200 and HS400 need the
 * controller's delay line trained against the card, and a tuning loop with
 * nothing to check it against is how one gets data that is wrong rather than
 * slow. The eMMC on the target board reports HS200 and HS400 support and
 * will run at 52 MHz eight bits wide, which is 52 MB/s of bus - far more
 * than this driver's PIO can feed anyway.
 */

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/spin.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <errno.h>
#include <limits.h>	/* sdmmcreg.h's __bitfield() wants UINT_MAX */
#include <stdint.h>
#include <string.h>

#include "sdmmc.h"
#include "sdmmcreg.h"

/*
 * An eMMC has no socket to publish an address, so the host names it. One,
 * because that is what every bootloader and both kernels use; any non-zero
 * value is legal, but on first contact with an unfamiliar part it is worth
 * differing from the known-good sequence in as few places as possible.
 */
#define SDMMC_EMMC_RCA		1

/* "The device must complete its initialization within 1 second of the first
 * CMD1 issued with a valid OCR range" (JESD84). Twice that, as emmc.c does. */
#define OCR_TIMEOUT_US		2000000

/* The voltage window this driver asks for: 2.7V to 3.6V, the whole of it. */
#define OCR_VOLTAGE_WINDOW	0x00ff8000

/*
 * Card status bit 7: the last SWITCH was refused. sdmmcreg.h names two bits
 * of the status word and this is the third one anything here looks at.
 */
#define MMC_R1_SWITCH_ERROR	(1 << 7)

/* CMD8 for an SD card: 2.7-3.6V, and a pattern to echo back. */
#define SD_IF_COND_ARG		0x000001aa
#define SD_IF_COND_PATTERN	0x000001ff

static struct sdmmc_host *host;
static struct sdmmc_card *card;

/*
 * Ceilings on how far the card is taken, both settable from the service's
 * arguments as buswidth= and maxmhz=.
 *
 * Not a tuning knob: it is how a data path is brought up on hardware nobody
 * has driven. Identification and transfer are different mechanisms - one
 * uses the command line, the other the data lines - and when the second
 * fails the useful question is whether it fails at one bit and the slowest
 * clock too. Answering that by rebuilding with different constants wastes a
 * boot each time; answering it from the command line costs nothing.
 */
static unsigned limit_width = 8;
static uint32_t limit_hz = 52000000;

/*
 * Every mandatory step of identification, with its name attached.
 *
 * Without this the whole sequence answers with one number - "no usable
 * card: -60" - and sixty is ETIMEDOUT, which every one of a dozen commands
 * can produce. Naming the step costs one string per call site and turns a
 * boot failure on an unfamiliar board from a guess into a fact.
 */
#define STEP(what, expr)						\
	do {								\
		if ((r = (expr)) != OK) {				\
			log_warn(&sdmmc_log, "%s: %d\n", (what), r);	\
			return r;					\
		}							\
	} while (0)

/*
 * The EXT_CSD register, kept because three separate questions are answered
 * out of it and re-reading costs a data transfer each time. Aligned because
 * the FIFO is read a word at a time.
 */
static uint8_t ext_csd[512] __attribute__((aligned(4)));

static int
send(uint8_t index, uint32_t arg, uint8_t rsp_type, struct sdmmc_cmd *out)
{
	struct sdmmc_cmd cmd;
	int r;

	memset(&cmd, 0, sizeof(cmd));
	cmd.index = index;
	cmd.arg = arg;
	cmd.rsp_type = rsp_type;
	cmd.data_dir = SDMMC_DATA_NONE;

	r = host->command(&cmd);
	if (out != NULL)
		*out = cmd;
	return r;
}

static int
send_data(uint8_t index, uint32_t arg, uint8_t rsp_type, int write, void *buf,
	uint32_t blocks, int stop, struct sdmmc_cmd *out)
{
	struct sdmmc_cmd cmd;
	int r;

	memset(&cmd, 0, sizeof(cmd));
	cmd.index = index;
	cmd.arg = arg;
	cmd.rsp_type = rsp_type;
	cmd.data_dir = write ? SDMMC_DATA_WRITE : SDMMC_DATA_READ;
	cmd.data = buf;
	cmd.blocks = blocks;
	cmd.blocklen = SDMMC_SECTOR_SIZE;
	cmd.stop = stop;

	r = host->command(&cmd);
	if (out != NULL)
		*out = cmd;
	return r;
}

/* An application command: CMD55 with the card's address, then the command. */
static int
send_app(uint8_t index, uint32_t arg, uint8_t rsp_type, struct sdmmc_cmd *out)
{
	int r;

	if ((r = send(MMC_APP_CMD, MMC_ARG_RCA(card->rca), SDMMC_RSP_R1,
	    NULL)) != OK)
		return r;
	return send(index, arg, rsp_type, out);
}

/* The card status bits that mean the last command went wrong. */
static int
check_r1(const struct sdmmc_cmd *cmd)
{
	uint32_t st = MMC_R1(cmd->resp);

	if ((st & 0xfdffa080) == 0)
		return OK;

	log_warn(&sdmmc_log, "CMD%u: card status 0x%08x\n", cmd->index, st);
	return EIO;
}

/*
 * Wait until the card is out of the programming state and ready for data.
 * Used where a command's own busy period is not the whole story - after a
 * SWITCH, and before calling a flush done.
 */
static int
wait_ready(uint32_t usecs)
{
	struct sdmmc_cmd cmd;
	spin_t s;
	uint32_t st;

	spin_init(&s, usecs);
	do {
		if (send(MMC_SEND_STATUS, MMC_ARG_RCA(card->rca),
		    SDMMC_RSP_R1, &cmd) != OK)
			return EIO;
		st = MMC_R1(cmd.resp);
		if (st & MMC_R1_SWITCH_ERROR) {
			log_warn(&sdmmc_log, "the card rejected a switch\n");
			return EIO;
		}
		if (st & MMC_R1_READY_FOR_DATA)
			return OK;
	} while (spin_check(&s));

	return ETIMEDOUT;
}

/* One byte of the EXT_CSD, written through CMD6. */
static int
mmc_switch(uint8_t index, uint8_t value)
{
	uint32_t arg;
	int r;

	arg = (MMC_SWITCH_MODE_WRITE_BYTE << 24) | ((uint32_t)index << 16) |
	    ((uint32_t)value << 8);

	if ((r = send(MMC_SWITCH, arg, SDMMC_RSP_R1B, NULL)) != OK)
		return r;
	return wait_ready(1000000);
}

/*
 * An all-ones response is not a card, it is the bus with its pull-ups: no
 * device drove the line and the controller called the command complete
 * anyway. Worth naming, because a driver that does not notice carries on and
 * blames whatever fails next. The low byte is the CRC's place, which the
 * host shifted in as zero.
 */
static int
all_ones_cid(const uint32_t *resp)
{
	return (resp[3] & 0x00ffffff) == 0x00ffffff && resp[2] == ~0u &&
	    resp[1] == ~0u && (resp[0] | 0xff) == ~0u;
}

/*
 * CMD0, and the pause the specification asks for afterwards. Sent twice over
 * the life of an identification: once at the start, and once more when the
 * SD branch has been tried and failed, because a card that answered nothing
 * may still have been left mid-sequence.
 */
static int
go_idle(void)
{
	int r;

	/*
	 * Two arguments, in order. CMD0 with 0xf0f0f0f0 is GO_PRE_IDLE_STATE:
	 * JEDEC gives it to bring a device back from states a plain CMD0 does
	 * not cover, and it costs one command. This matters here because the
	 * device is not being met cold - the bootloader has already had it at
	 * HS200, eight bits wide - and a device that did not hear CMD0 says
	 * nothing about it: the command has no response, so the first sign of
	 * trouble is several commands later.
	 */
	(void)send(MMC_GO_IDLE_STATE, 0xf0f0f0f0, SDMMC_RSP_NONE, NULL);
	micro_delay(2000);
	r = send(MMC_GO_IDLE_STATE, 0, SDMMC_RSP_NONE, NULL);
	micro_delay(2000);
	return r;
}

/* The SD branch: CMD8 to learn the version, then ACMD41 until ready. */
static int
sd_op_cond(int *found)
{
	struct sdmmc_cmd cmd;
	uint32_t arg;
	spin_t s;
	int v2;

	*found = 0;

	v2 = 0;
	if (send(SD_SEND_IF_COND, SD_IF_COND_ARG, SDMMC_RSP_R7, &cmd) == OK) {
		if ((MMC_R7(cmd.resp) & 0xfff) != (SD_IF_COND_ARG & 0xfff)) {
			log_warn(&sdmmc_log, "CMD8 echoed 0x%08x\n",
			    MMC_R7(cmd.resp));
			return EIO;
		}
		v2 = 1;
	}

	/*
	 * The card's address is not known yet, and CMD55 before the card has
	 * one is addressed to zero - which is what the specification says an
	 * idle card answers to.
	 */
	card->rca = 0;
	arg = OCR_VOLTAGE_WINDOW | (v2 ? MMC_OCR_HCS : 0);

	spin_init(&s, OCR_TIMEOUT_US);
	do {
		if (send_app(SD_APP_OP_COND, arg, SDMMC_RSP_R3, &cmd) != OK)
			return OK;	/* not an SD card; try eMMC */
		if (MMC_R3(cmd.resp) & MMC_OCR_MEM_READY) {
			card->is_sd = 1;
			card->sector_addressed =
			    (MMC_R3(cmd.resp) & MMC_OCR_HCS) != 0;
			*found = 1;
			return OK;
		}
		micro_delay(1000);
	} while (spin_check(&s));

	log_warn(&sdmmc_log, "an SD card answered but never became ready\n");
	return ETIMEDOUT;
}

/* The eMMC branch: CMD1 until ready. */
static int
mmc_op_cond(int *found)
{
	struct sdmmc_cmd cmd;
	struct sdmmc_cmd probe;
	spin_t s;
	uint32_t ocr;
	unsigned tries;

	*found = 0;

	/* The first CMD1 asks; it does not tell. */
	if (send(MMC_SEND_OP_COND, 0, SDMMC_RSP_R3, &cmd) != OK)
		return OK;	/* nothing there at all */

	/*
	 * What to ask for is the intersection, not the wish.
	 *
	 * JEDEC is blunt about this: a device sent an operating range it does
	 * not support goes to the Inactive state, and an inactive device
	 * answers nothing ever again. So the range in every CMD1 after the
	 * first is the one the device just named, narrowed by what this
	 * driver would accept - which is what U-Boot and Linux both do, and
	 * for the same reason.
	 *
	 * It cost a day to find, because the failure does not look like a
	 * refusal. The device answers the first few CMD1s, reports itself
	 * ready, and only then falls silent: CMD2 comes back as all ones,
	 * which is an idle bus read through its pull-ups, and the controller
	 * calls that a completed command. The card was already gone.
	 */
	ocr = MMC_R3(cmd.resp) & OCR_VOLTAGE_WINDOW;
	if (ocr == 0) {
		log_warn(&sdmmc_log, "the card names no voltage range; "
		    "asking for all of ours\n");
		ocr = OCR_VOLTAGE_WINDOW;
	}
	log_debug(&sdmmc_log, "the card's range is %08x\n", ocr);

	tries = 0;
	spin_init(&s, OCR_TIMEOUT_US);
	do {
		tries++;
		if (send(MMC_SEND_OP_COND, MMC_OCR_HCS | ocr,
		    SDMMC_RSP_R3, &cmd) != OK)
			return EIO;
		if (MMC_R3(cmd.resp) & MMC_OCR_MEM_READY) {
			card->is_sd = 0;
			/*
			 * Bit 30 of the OCR is the card's answer, not an
			 * echo of the request: a device of 2 GiB or less
			 * clears it and is addressed by byte.
			 */
			card->sector_addressed =
			    (MMC_R3(cmd.resp) & MMC_OCR_HCS) != 0;
			log_debug(&sdmmc_log, "OCR %08x after %u CMD1\n",
			    MMC_R3(cmd.resp), tries);

			/*
			 * Ask once more, now that the device says it is
			 * done. CMD1 is a command of the Idle state: a
			 * device that has moved on to Ready should ignore
			 * it. If it answers anyway then it never left Idle,
			 * whatever the busy bit claimed - and that, not the
			 * command after it, is where to look.
			 */
			if (send(MMC_SEND_OP_COND, 0, SDMMC_RSP_R3,
			    &probe) == OK)
				log_debug(&sdmmc_log, "still answers CMD1 "
				    "when ready: %08x\n", MMC_R3(probe.resp));
			else
				log_debug(&sdmmc_log, "ignores CMD1 once "
				    "ready, as it should\n");
			*found = 1;
			return OK;
		}
		micro_delay(1000);
	} while (spin_check(&s));

	log_warn(&sdmmc_log, "an eMMC answered but never became ready\n");
	return ETIMEDOUT;
}

/* Capacity, which is asked of the CSD or, past 2 GiB, of the EXT_CSD. */
static void
read_capacity(void)
{
	uint32_t sec_count;

	if (card->is_sd) {
		if (SD_CSD_CSDVER(card->csd) == SD_CSD_CSDVER_2_0)
			card->sectors = (uint64_t)SD_CSD_V2_CAPACITY(card->csd);
		else
			card->sectors = (uint64_t)SD_CSD_CAPACITY(card->csd) *
			    ((uint64_t)1 << SD_CSD_READ_BL_LEN(card->csd)) /
			    SDMMC_SECTOR_SIZE;
		return;
	}

	/*
	 * The CSD of an eMMC larger than 2 GiB cannot say how large it is:
	 * C_SIZE saturates at 0xfff, and the real number lives in SEC_COUNT
	 * of the EXT_CSD. Trusting SEC_COUNT whenever it is not zero is what
	 * every driver does, because it is also right for smaller devices.
	 */
	sec_count = (uint32_t)ext_csd[EXT_CSD_SEC_COUNT] |
	    ((uint32_t)ext_csd[EXT_CSD_SEC_COUNT + 1] << 8) |
	    ((uint32_t)ext_csd[EXT_CSD_SEC_COUNT + 2] << 16) |
	    ((uint32_t)ext_csd[EXT_CSD_SEC_COUNT + 3] << 24);

	if (sec_count != 0)
		card->sectors = sec_count;
	else
		card->sectors = (uint64_t)MMC_CSD_CAPACITY(card->csd) *
		    ((uint64_t)1 << MMC_CSD_READ_BL_LEN(card->csd)) /
		    SDMMC_SECTOR_SIZE;
}

/*
 * Widen the bus and raise the clock, for an eMMC.
 *
 * The card is told first and the controller second, in that order: the reply
 * to CMD6 comes back on the command line, which is one bit wide whatever the
 * data bus is doing, so the card can be moved to eight bits and answer about
 * it before the host follows.
 */
static void
mmc_tune(void)
{
	unsigned width;
	uint32_t hz;

	hz = 52000000;
	if (hz > limit_hz)
		hz = limit_hz;
	if (hz > host->max_freq)
		hz = host->max_freq;

	if ((ext_csd[EXT_CSD_CARD_TYPE] & EXT_CSD_CARD_TYPE_HS_52) &&
	    hz > 26000000) {
		if (mmc_switch(EXT_CSD_HS_TIMING, 1) == OK) {
			(void)host->set_timing(1);
			if (host->set_clock(hz, &card->clock) == OK)
				card->high_speed = 1;
		} else {
			log_warn(&sdmmc_log, "the card refused high speed\n");
		}
	} else {
		(void)host->set_clock(hz, &card->clock);
	}

	width = host->max_bus_width;
	if (width > limit_width)
		width = limit_width;
	if (width > 8)
		width = 8;
	if (width >= 8)
		width = 8;
	else if (width >= 4)
		width = 4;
	else
		width = 1;

	if (width > 1) {
		uint8_t v = (width == 8) ? EXT_CSD_BUS_WIDTH_8 :
		    EXT_CSD_BUS_WIDTH_4;

		if (mmc_switch(EXT_CSD_BUS_WIDTH, v) == OK &&
		    host->set_bus_width(width) == OK)
			card->bus_width = width;
		else
			log_warn(&sdmmc_log, "staying on a one-bit bus\n");
	}
}

/*
 * The write cache of an eMMC, and why it is turned on.
 *
 * A device with the cache off answers a write when the data is on the
 * medium, which makes a flush nothing to do - honest, and slow. With the
 * cache on the write returns earlier and the flush becomes the thing that
 * the file system's journal stands on. That is the same argument the virtio
 * driver makes for acknowledging VIRTIO_BLK_F_FLUSH: a medium that never
 * holds anything back cannot prove that the ordering works. So the cache is
 * turned on here, and CMD6 on FLUSH_CACHE empties it in sdmmc_card_flush().
 *
 * It is turned on explicitly rather than left alone because its state is not
 * ours to assume: a power-on clears it, a warm reboot from a system that set
 * it does not, and the driver would otherwise not know which flush it owes.
 */
static void
mmc_enable_cache(void)
{
	uint32_t size;

	size = (uint32_t)ext_csd[EXT_CSD_CACHE_SIZE] |
	    ((uint32_t)ext_csd[EXT_CSD_CACHE_SIZE + 1] << 8) |
	    ((uint32_t)ext_csd[EXT_CSD_CACHE_SIZE + 2] << 16) |
	    ((uint32_t)ext_csd[EXT_CSD_CACHE_SIZE + 3] << 24);

	if (size == 0) {
		card->cache_on = 0;
		log_debug(&sdmmc_log, "the card has no write cache\n");
		return;
	}

	if (mmc_switch(EXT_CSD_CACHE_CTRL, 1) != OK) {
		card->cache_on = 0;
		log_warn(&sdmmc_log, "cannot turn the write cache on\n");
		return;
	}

	card->cache_on = 1;
	log_info(&sdmmc_log, "write cache on, %u KiB\n", size);
}

int
sdmmc_card_init(struct sdmmc_host *h, struct sdmmc_card *c)
{
	struct sdmmc_cmd cmd;
	unsigned attempt;
	int r, found;

	host = h;
	card = c;

	memset(card, 0, sizeof(*card));
	card->bus_width = 1;

	{
		long v;

		v = 8;
		(void)env_parse("buswidth", "d", 0, &v, 1, 8);
		limit_width = (unsigned)v;
		v = 52;
		(void)env_parse("maxmhz", "d", 0, &v, 1, 200);
		limit_hz = (uint32_t)v * 1000000;
		if (limit_width != 8 || limit_hz != 52000000)
			log_info(&sdmmc_log, "held to %u bits and %u MHz by "
			    "request\n", limit_width, limit_hz / 1000000);
	}

	STEP("cannot set the identification clock",
	    host->set_clock(SDMMC_IDENT_HZ, &card->clock));
	(void)host->set_bus_width(1);
	(void)host->set_timing(0);

	STEP("CMD0 (go idle)", go_idle());

	STEP("the SD branch", sd_op_cond(&found));
	if (!found) {
		STEP("CMD0 (go idle, again)", go_idle());
		STEP("CMD1 (MMC operating conditions)", mmc_op_cond(&found));
	}
	if (!found) {
		log_warn(&sdmmc_log, "no card answered\n");
		return ENODEV;
	}

	/*
	 * The card has just finished its own initialisation; give it the
	 * moment Linux gives it before asking anything else. Ten
	 * milliseconds is what mmc_delay() spends between operating-condition
	 * polls there, and nothing here is in a hurry once per boot.
	 */
	micro_delay(10000);

	/*
	 * CMD2 is a broadcast, and a broadcast whose answer is lost leaves
	 * nothing behind to ask again about - so it is worth asking again.
	 * Three attempts, with the identification restarted in between,
	 * because a card that has stopped listening will not start because
	 * the same question was repeated.
	 */
	for (attempt = 1; ; attempt++) {
		STEP("CMD2 (all send CID)",
		    send(MMC_ALL_SEND_CID, 0, SDMMC_RSP_R2, &cmd));
		if (!all_ones_cid(cmd.resp) || attempt == 3)
			break;
		log_warn(&sdmmc_log, "CMD2 attempt %u read an idle bus; "
		    "starting over\n", attempt);

		/*
		 * Ask the same question expecting a short answer. The card's
		 * reply does not change, but the controller's handling of it
		 * does: if a 48-bit read brings back something that is not
		 * all ones, then the card is answering and it is the 136-bit
		 * path that loses it, which is a different fault in a
		 * different place. Two lines to tell those apart is cheap
		 * next to guessing between them.
		 */
		if (send(MMC_ALL_SEND_CID, 0, SDMMC_RSP_R3, &cmd) == OK)
			log_warn(&sdmmc_log, "CMD2 as a 48-bit response: "
			    "%08x\n", MMC_R3(cmd.resp));
		else
			log_warn(&sdmmc_log, "CMD2 as a 48-bit response: "
			    "no answer at all\n");
		STEP("CMD0 (retry)", go_idle());
		STEP("CMD1 (retry)", mmc_op_cond(&found));
		if (!found)
			return ENODEV;
		micro_delay(10000);
	}
	memcpy(card->cid, cmd.resp, sizeof(card->cid));

	/*
	 * Printed as soon as it is read, and not later with the CSD, because
	 * this is the first thing the card says about itself: on a board
	 * nobody has driven before it settles at once whether the card is
	 * answering at all and whether the 136-bit response was put back
	 * together right. Linux prints the same bytes in the same order in
	 * /sys/class/mmc_host/.../cid, so the two can be held side by side.
	 */
	log_debug(&sdmmc_log, "CID %08x%08x%08x%08x\n", card->cid[3],
	    card->cid[2], card->cid[1], card->cid[0]);

	if (all_ones_cid(card->cid)) {
		log_warn(&sdmmc_log, "CMD2 read an idle bus: no card is "
		    "answering\n");
		return ENODEV;
	}

	if (card->is_sd) {
		STEP("CMD3 (publish RCA)",
		    send(SD_SEND_RELATIVE_ADDR, 0, SDMMC_RSP_R6, &cmd));
		card->rca = SD_R6_RCA(cmd.resp);
	} else {
		card->rca = SDMMC_EMMC_RCA;
		STEP("CMD3 (set RCA)",
		    send(MMC_SET_RELATIVE_ADDR, MMC_ARG_RCA(card->rca),
		    SDMMC_RSP_R1, &cmd));
	}

	STEP("CMD9 (send CSD)",
	    send(MMC_SEND_CSD, MMC_ARG_RCA(card->rca), SDMMC_RSP_R2, &cmd));
	memcpy(card->csd, cmd.resp, sizeof(card->csd));

	/*
	 * Printed raw, most significant word first, which is the order
	 * Linux prints them in /sys/class/mmc_host/.../cid and .../csd. On
	 * first contact with a new part that is the cheapest check there is
	 * that the host put the response together right - a wrong shift
	 * shows up here as recognisable numbers moved by a byte, long
	 * before it shows up as a card of the wrong size.
	 */
	log_debug(&sdmmc_log, "CSD %08x%08x%08x%08x\n", card->csd[3],
	    card->csd[2], card->csd[1], card->csd[0]);

	STEP("CMD7 (select card)",
	    send(MMC_SELECT_CARD, MMC_ARG_RCA(card->rca), SDMMC_RSP_R1B, &cmd));

	if (card->is_sd) {
		SD_CID_PNM_CPY(card->cid, card->name);
	} else {
		/*
		 * "V2" is NetBSD's name for the layout an eMMC of version 4
		 * and later uses: an eight-bit manufacturer identifier and a
		 * six-character product name at bits 103 to 56. The V1
		 * macros read seven characters starting a byte earlier,
		 * which on a modern part returns half the OEM identifier and
		 * a name shifted by one.
		 */
		MMC_CID_PNM_V2_CPY(card->cid, card->name);

		/* Everything past this point is read out of the EXT_CSD. */
		memset(ext_csd, 0, sizeof(ext_csd));
		STEP("CMD8 (send EXT_CSD)",
		    send_data(MMC_SEND_EXT_CSD, 0, SDMMC_RSP_R1, 0, ext_csd, 1,
		    0, &cmd));
		STEP("CMD8 status", check_r1(&cmd));
	}

	read_capacity();
	if (card->sectors == 0) {
		log_warn(&sdmmc_log, "the card reports no capacity\n");
		return EIO;
	}

	/*
	 * The block length only means anything to a byte-addressed card, and
	 * 512 is the reset value on every other; setting it costs one
	 * command and removes a case.
	 */
	STEP("CMD16 (set block length)",
	    send(MMC_SET_BLOCKLEN, SDMMC_SECTOR_SIZE, SDMMC_RSP_R1, &cmd));

	if (card->is_sd) {
		/*
		 * Four bits and the card's default speed. The SD high speed
		 * switch is CMD6 with a 64-byte data phase and its own status
		 * structure, and there is no SD card on the target board to
		 * try it against; the socket there is behind the other
		 * controller, which this driver does not yet drive.
		 */
		if (host->max_bus_width >= 4 &&
		    send_app(SD_APP_SET_BUS_WIDTH, SD_ARG_BUS_WIDTH_4,
		    SDMMC_RSP_R1, &cmd) == OK &&
		    host->set_bus_width(4) == OK)
			card->bus_width = 4;
		(void)host->set_clock(25000000, &card->clock);
	} else {
		mmc_tune();
		mmc_enable_cache();
	}

	card->present = 1;

	log_info(&sdmmc_log, "%s \"%s\": %llu sectors (%llu MiB), "
	    "%u-bit bus at %u Hz%s\n", card->is_sd ? "SD card" : "eMMC",
	    card->name, (unsigned long long)card->sectors,
	    (unsigned long long)(card->sectors / 2048), card->bus_width,
	    card->clock, card->high_speed ? ", high speed" : "");
	return OK;
}

/*
 * Tell the card how many blocks are coming, so that it stops by itself.
 *
 * The alternative is to let the controller send CMD12 at the end, which the
 * standard provides for and this part gets wrong: every multi-block read
 * came back with the auto-command reported as both not executed and timed
 * out. CMD23 is mandatory on an eMMC of version 4.4 and later, Linux uses
 * it on this very card, and it removes the whole mechanism rather than
 * working around it. An SD card that does not offer CMD23 falls back to the
 * stop command; none is attached here, and that is the only reason the
 * fallback is untried.
 */
static int
set_block_count(uint32_t count)
{
	struct sdmmc_cmd cmd;
	int r;

	if ((r = send(MMC_SET_BLOCK_COUNT, count, SDMMC_RSP_R1, &cmd)) != OK)
		return r;
	return check_r1(&cmd);
}

/* Sector or byte, as the card said when it came up. */
static uint32_t
data_address(uint64_t sector)
{
	if (card->sector_addressed)
		return (uint32_t)sector;
	return (uint32_t)(sector * SDMMC_SECTOR_SIZE);
}

/* How much of a request one command may carry. */
static uint32_t
run_length(uint32_t count)
{
	uint32_t n = count;

	if (host->max_blocks != 0 && n > host->max_blocks)
		n = host->max_blocks;
	return n;
}

int
sdmmc_card_read(uint64_t sector, uint32_t count, void *buf)
{
	struct sdmmc_cmd cmd;
	uint32_t n;
	int r;

	if (!card->present)
		return ENXIO;
	if (count == 0)
		return OK;
	if (sector + count > card->sectors)
		return EINVAL;

	while (count > 0) {
		n = run_length(count);

		if (n > 1 && (r = set_block_count(n)) != OK)
			return r;

		r = send_data(n > 1 ? MMC_READ_BLOCK_MULTIPLE :
		    MMC_READ_BLOCK_SINGLE, data_address(sector),
		    SDMMC_RSP_R1, 0, buf, n, 0, &cmd);
		if (r != OK)
			return r;
		if ((r = check_r1(&cmd)) != OK)
			return r;

		sector += n;
		count -= n;
		buf = (uint8_t *)buf + n * SDMMC_SECTOR_SIZE;
	}

	return OK;
}

int
sdmmc_card_write(uint64_t sector, uint32_t count, const void *buf)
{
	struct sdmmc_cmd cmd;
	uint32_t n;
	int r;

	if (!card->present)
		return ENXIO;
	if (count == 0)
		return OK;
	if (sector + count > card->sectors)
		return EINVAL;

	while (count > 0) {
		n = run_length(count);

		if (n > 1 && (r = set_block_count(n)) != OK)
			return r;

		r = send_data(n > 1 ? MMC_WRITE_BLOCK_MULTIPLE :
		    MMC_WRITE_BLOCK_SINGLE, data_address(sector),
		    SDMMC_RSP_R1, 1, (void *)(uintptr_t)buf, n, 0, &cmd);
		if (r != OK)
			return r;
		if ((r = check_r1(&cmd)) != OK)
			return r;

		sector += n;
		count -= n;
		buf = (const uint8_t *)buf + n * SDMMC_SECTOR_SIZE;
	}

	return OK;
}

/*
 * Empty whatever the card is holding back.
 *
 * With no cache there is nothing to do and OK is the truth, not a shrug: a
 * write on such a card is not answered until the data is on the medium, and
 * this driver waits for that answer. With a cache, FLUSH_CACHE is a byte of
 * the EXT_CSD and the card holds the bus busy until it is done.
 */
int
sdmmc_card_flush(void)
{
	if (!card->present)
		return ENXIO;

	if (!card->cache_on)
		return wait_ready(1000000);

	return mmc_switch(EXT_CSD_FLUSH_CACHE, 1);
}
