/*
 * Host test for minix/drivers/power/qcom_pmic/spmi.c against a model of the
 * SPMI PMIC arbiter v2/v3 as port/clover/hw-spmi.md describes it.
 *
 * The regions are plain memory.  A transaction cannot be seen when spmi.c
 * writes CMD, so the model runs it in delay_us(), which spmi.c calls while
 * it waits: every channel whose CMD is no longer the idle sentinel is
 * executed against a register file per SID, STATUS is set and CMD put back.
 *
 *	cc -I<qcom_pmic> spmitest.c <qcom_pmic>/spmi.c && ./a.out
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "spmi.h"

#define CORE_SZ		0x1000
#define CNFG_SZ		0x3000
#define WIN_SZ		0x1000000
#define IDLE		0xdeadbeefu

static uint8_t core[CORE_SZ], cnfg[CNFG_SZ];
static uint8_t *obs, *chn;
static uint8_t pmic[16][65536];
static int respond = 1;		/* 0: never finish (timeout) */
static uint32_t force_status;	/* nonzero: STATUS to report instead */
static int napid_used = 8;
static unsigned ee = 0;
static int fails;

#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", \
    __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t *
reg(uint8_t *base, size_t off)
{
	return (uint32_t *)(base + off);
}

static void
model_run(uint8_t *region, int writes)
{
	int a;

	for (a = 0; a < napid_used; a++) {
		uint8_t *ch = region + (size_t)a * 0x8000 + (size_t)ee * 0x1000;
		uint32_t cmd = *reg(ch, 0), tab, ppid, sid, addr, len, opc, i;
		uint32_t d0 = 0, d1 = 0;

		if (cmd == IDLE)
			continue;
		*reg(ch, 0) = IDLE;
		if (!respond)
			continue;
		if (force_status) {
			*reg(ch, 8) = force_status;
			continue;
		}
		tab = *reg(core, 0x800 + 4 * a);
		ppid = (tab >> 8) & 0xfff;
		sid = ppid >> 8;
		addr = (ppid & 0xff) << 8 | ((cmd >> 4) & 0xff);
		len = (cmd & 7) + 1;
		opc = cmd >> 27;
		if (writes) {
			if ((*reg(cnfg, 0x700 + 4 * a) & 7) != ee) {
				*reg(ch, 8) = 1 | 4;	/* denied */
				continue;
			}
			if (opc != 0) {
				*reg(ch, 8) = 1 | 2;
				continue;
			}
			d0 = *reg(ch, 0x10);
			d1 = *reg(ch, 0x14);
			for (i = 0; i < len; i++)
				pmic[sid][(addr + i) & 0xffff] = (uint8_t)
				    (i < 4 ? d0 >> 8 * i : d1 >> 8 * (i - 4));
		} else {
			if (opc != 1) {
				*reg(ch, 8) = 1 | 2;
				continue;
			}
			for (i = 0; i < len; i++) {
				uint32_t b = pmic[sid][(addr + i) & 0xffff];

				if (i < 4)
					d0 |= b << 8 * i;
				else
					d1 |= b << 8 * (i - 4);
			}
			*reg(ch, 0x18) = d0;
			*reg(ch, 0x1c) = d1;
		}
		*reg(ch, 8) = 1;
	}
}

static unsigned delays;

static void
delay_us(unsigned us)
{
	(void)us;
	delays++;
	model_run(obs, 0);
	model_run(chn, 1);
}

static void
setup(uint32_t version)
{
	int a;
	/* APID -> (sid, upper), owner EE */
	static const struct { unsigned sid, up, owner; } map[8] = {
		{ 0, 0x01, 0 },		/* REVID */
		{ 0, 0x08, 0 },		/* PON */
		{ 2, 0xc6, 0 },		/* PM660L GPIO7 */
		{ 3, 0xd8, 0 },		/* WLED ctrl */
		{ 3, 0xd9, 0 },		/* WLED sink */
		{ 0, 0x40, 0 },		/* FG */
		{ 0, 0x10, 2 },		/* charger: owned by another EE */
		{ 0, 0x41, 0 },		/* FG batt info */
	};

	memset(core, 0, sizeof(core));
	memset(cnfg, 0, sizeof(cnfg));
	memset(pmic, 0, sizeof(pmic));
	*reg(core, 0) = version;
	for (a = 0; a < 8; a++) {
		*reg(core, 0x800 + 4 * a) = (map[a].sid << 8 | map[a].up) << 8 |
		    0x5a;	/* low byte: noise the driver must ignore */
		*reg(cnfg, 0x700 + 4 * a) = map[a].owner | 0x10;
	}
	for (a = 0; a < 8; a++) {
		*reg(obs, (size_t)a * 0x8000 + ee * 0x1000) = IDLE;
		*reg(chn, (size_t)a * 0x8000 + ee * 0x1000) = IDLE;
	}
	respond = 1;
	force_status = 0;
}

/*
 * What the hardware does on a CMD write and the model cannot see: STATUS
 * goes back to "not done".  Called before every operation below; without
 * it the driver would read the last transaction's "done".
 */
static void
fresh_status(void)
{
	int a;

	for (a = 0; a < napid_used; a++) {
		*reg(obs, (size_t)a * 0x8000 + ee * 0x1000 + 8) = 0;
		*reg(chn, (size_t)a * 0x8000 + ee * 0x1000 + 8) = 0;
	}
}

static int
RD(struct spmi *s, unsigned sid, unsigned addr, uint8_t *buf, unsigned len)
{
	fresh_status();
	return spmi_read(s, sid, addr, buf, len);
}

static int
WR(struct spmi *s, unsigned sid, unsigned addr, const uint8_t *buf,
	unsigned len)
{
	fresh_status();
	return spmi_write(s, sid, addr, buf, len);
}

static void
init_spmi(struct spmi *s)
{
	memset(s, 0, sizeof(*s));
	s->core = core; s->core_size = CORE_SZ;
	s->cnfg = cnfg; s->cnfg_size = CNFG_SZ;
	s->obsrvr = obs; s->obsrvr_size = WIN_SZ;
	s->chnls = chn; s->chnls_size = WIN_SZ;
	s->ee = ee;
	s->delay_us = delay_us;
}

int
main(void)
{
	static struct spmi s;
	uint8_t b[8], w[8];
	int r, i;

	obs = calloc(1, WIN_SZ);
	chn = calloc(1, WIN_SZ);

	/* Versions: v1 and v5 refused, v2 and v3 taken. */
	setup(0x20000000); init_spmi(&s);
	CHECK(spmi_init(&s) == ENOTSUP, "v1 accepted");
	setup(0x50000000); init_spmi(&s);
	CHECK(spmi_init(&s) == ENOTSUP, "v5 accepted");
	setup(0x20010000); init_spmi(&s);
	CHECK(spmi_init(&s) == OK_SPMI, "v2 refused");
	setup(0x30010000); init_spmi(&s);
	CHECK(spmi_init(&s) == OK_SPMI, "v3 refused");
	CHECK(s.napid == 512, "napid %u", s.napid);

	/* Reads of 1, 2, 4, 5 and 8 bytes. */
	for (i = 0; i < 8; i++)
		pmic[0][0x4009 + i] = (uint8_t)(0xa0 + i);
	pmic[0][0x0810] = 0x02;
	CHECK(RD(&s, 0, 0x810, b, 1) == OK_SPMI && b[0] == 0x02,
	    "read PON %02x", b[0]);
	for (i = 1; i <= 8; i++) {
		memset(b, 0, sizeof(b));
		r = RD(&s, 0, 0x4009, b, i);
		CHECK(r == OK_SPMI, "read %d: %d", i, r);
		CHECK(memcmp(b, &pmic[0][0x4009], i) == 0, "read %d bytes "
		    "wrong", i);
	}
	CHECK(RD(&s, 0, 0x4009, b, 0) == EINVAL, "len 0");
	CHECK(RD(&s, 0, 0x4009, b, 9) == EINVAL, "len 9");

	/* Unmapped peripheral, bad SID. */
	CHECK(RD(&s, 0, 0x2400, b, 1) == ENODEV, "unmapped read");
	CHECK(RD(&s, 16, 0x810, b, 1) == ENODEV, "sid 16");
	CHECK(!spmi_present(&s, 1, 0x810), "present on sid 1");
	CHECK(spmi_present(&s, 3, 0xd946), "WLED sink absent");

	/* Writes: 1 and 6 bytes land; the other EE's charger is refused. */
	w[0] = 0x80;
	CHECK(WR(&s, 3, 0xd846, w, 1) == OK_SPMI &&
	    pmic[3][0xd846] == 0x80, "write WLED en");
	for (i = 0; i < 6; i++)
		w[i] = (uint8_t)(0x10 + i);
	CHECK(WR(&s, 3, 0xd957, w, 6) == OK_SPMI &&
	    memcmp(&pmic[3][0xd957], w, 6) == 0, "write 6 bytes");
	CHECK(!spmi_writable(&s, 0, 0x1006), "charger writable");
	pmic[0][0x1043] = 0x55;
	w[0] = 1;
	CHECK(WR(&s, 0, 0x1043, w, 1) == EPERM &&
	    pmic[0][0x1043] == 0x55, "foreign write went through");
	CHECK(RD(&s, 0, 0x1006, b, 1) == OK_SPMI,
	    "observer read of a foreign peripheral refused");

	/* Status bits. */
	force_status = 1 | 2;
	CHECK(RD(&s, 0, 0x810, b, 1) == EIO, "failure");
	force_status = 1 | 4;
	CHECK(RD(&s, 0, 0x810, b, 1) == EPERM, "denied");
	force_status = 1 | 8;
	CHECK(RD(&s, 0, 0x810, b, 1) == EIO, "dropped");
	force_status = 0;

	/* No answer: a bounded wait. */
	respond = 0;
	delays = 0;
	CHECK(RD(&s, 0, 0x810, b, 1) == ETIMEDOUT, "no timeout");
	CHECK(delays == 1000, "waited %u us", delays);
	respond = 1;

	/* A window too small for an APID: refused, nothing touched. */
	s.obsrvr_size = 0x8000 * 2;
	CHECK(RD(&s, 3, 0xd846, b, 1) == ENODEV, "out of window");
	s.obsrvr_size = WIN_SZ;

	/* EE 1: channels at +0x1000, its own ownership. */
	ee = 1;
	setup(0x30000000); init_spmi(&s);
	CHECK(spmi_init(&s) == OK_SPMI, "ee1 init");
	CHECK(!spmi_writable(&s, 3, 0xd846), "ee1 may write ee0's");
	pmic[0][0x810] = 0x01;
	CHECK(RD(&s, 0, 0x810, b, 1) == OK_SPMI && b[0] == 0x01,
	    "ee1 read");
	ee = 0;

	printf("%s: %d failure%s\n", fails ? "FAIL" : "ok", fails,
	    fails == 1 ? "" : "s");
	return fails != 0;
}
