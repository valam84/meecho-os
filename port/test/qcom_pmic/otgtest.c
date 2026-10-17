/*
 * Host test of the PMIC driver's boost policy (qcom_pmic/typec.c,
 * typec_otg_set() and typec_otg_poll()) against a model of the PM660
 * charger's Type-C and OTG blocks.
 *
 * The model is written from what the tablet's own Android charger driver
 * does and expects (port/clover/hw-usbhost.md, section 3), not from the
 * code under test: CMD_OTG starts the boost, whose soft start finishes -
 * OTG_STATUS bit 3, state 2 - only at a current limit at least as high as
 * the load needs; the boost's VBUS raises USBIN's plug-in bit like any
 * VBUS; a write to a secured register (0xa0 and up in its peripheral)
 * that is not right after 0xa5 to the peripheral's 0xd0 is an error.  Every
 * write is logged, so a scenario can say exactly what was written.
 *
 * Checked: with the policy off nothing is ever written, whatever is
 * plugged in; "auto" makes the port dual role and the soft start slow, and
 * with the hub's cable (Rp, VBUS from outside) writes nothing more; an OTG
 * adapter (Rd) seen twice in a row gets USBIN asleep, the halt mode, the
 * steps of the PM660's work-around from 250 mA until the soft start
 * finishes, and the limit (500 mA) after it; a soft start that never
 * finishes leaves everything off and is not tried again until the device
 * is unplugged; so does an overload; the device leaving switches it off
 * in the right order; a sink with VBUS already there is not boosted; and
 * "off" puts back the power role the port had.  The PMI8998 has no steps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typec.h"

static unsigned checks, failures;

#define CHECK(cond, ...) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("  FAIL: ");					\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

/*===========================================================================*
 *    the model                                                              *
 *===========================================================================*/
#define MLOG	256

static struct {
	uint8_t r[0x1000];
	unsigned load_ma;		/* the soft start needs this limit */
	int unlocked;			/* the last write was 0xa5 to xxd0 */
	unsigned started_at;		/* model time CMD_OTG went to 1 */
	unsigned long t;		/* microseconds of delay */
	int ext_vbus;			/* the other end gives VBUS */
	unsigned errors;
	struct { unsigned off; uint8_t v; } log[MLOG];
	unsigned nlog;
} M;

static const unsigned cl_ma[8] = { 250, 500, 750, 1000, 1250, 1500, 1750,
	2000 };

static void
merr(const char *what, unsigned off)
{
	M.errors++;
	printf("  model: %s (0x%03x)\n", what, off);
}

/* VBUS on the port: ours, if the boost runs, or the other end's. */
static void
vbus_update(void)
{
	int ours = (M.r[TC_OTG_STATUS] & 7) == 2;

	if (ours || M.ext_vbus) {
		M.r[TC_USBIN_RT_STS] |= TC_USBIN_PLUGIN;
		M.r[TC_STATUS_1 + 3] |= TC_ST4_VBUS;
	} else {
		M.r[TC_USBIN_RT_STS] &= ~TC_USBIN_PLUGIN;
		M.r[TC_STATUS_1 + 3] &= ~TC_ST4_VBUS;
	}
}

static int
m_rd(void *ctx, unsigned off, uint8_t *buf, unsigned n)
{
	(void)ctx;
	if (n == 0 || n > 8 || off + n > sizeof(M.r))
		return -1;
	memcpy(buf, M.r + off, n);
	return 0;
}

static int
m_wr(void *ctx, unsigned off, uint8_t v)
{
	(void)ctx;
	if (off >= sizeof(M.r))
		return -1;
	if (M.nlog < MLOG) {
		M.log[M.nlog].off = off;
		M.log[M.nlog].v = v;
	}
	M.nlog++;

	if ((off & 0xff) == 0xd0) {
		M.unlocked = v == 0xa5;
		return 0;
	}
	if ((off & 0xff) >= 0xa0 && !M.unlocked)
		merr("a secured register written without 0xa5 first", off);
	M.unlocked = 0;

	M.r[off] = v;
	if (off == TC_CMD_OTG) {
		if (v & 1) {
			M.started_at = (unsigned)M.t;
			M.r[TC_OTG_STATUS] = 0x01;	/* starting */
		} else
			M.r[TC_OTG_STATUS] = 0x00;
		vbus_update();
	}
	return 0;
}

static int
m_writable(void *ctx, unsigned off)
{
	(void)ctx;
	(void)off;
	return 1;
}

/* Time passes: a running soft start finishes, if the limit allows. */
static void
m_delay(void *ctx, unsigned us)
{
	(void)ctx;
	M.t += us;
	if ((M.r[TC_CMD_OTG] & 1) && (M.r[TC_OTG_STATUS] & 7) == 1 &&
	    M.t - M.started_at >= 3000 &&
	    cl_ma[M.r[TC_OTG_CL] & 7] >= M.load_ma) {
		M.r[TC_OTG_STATUS] = 0x08 | 0x02;	/* done, state on */
		vbus_update();
	}
}

static struct typec_io io = {
	.rd = m_rd, .wr = m_wr, .writable = m_writable, .delay_us = m_delay,
	.base = 0x1000
};

/* What is at the other end: nothing, the hub's cable, an OTG adapter. */
#define P_NONE		0
#define P_HUB		1
#define P_ADAPTER	2

static void
plug(int what)
{
	M.r[TC_STATUS_1] = 0;
	M.r[TC_STATUS_1 + 1] = 0;
	M.r[TC_STATUS_1 + 3] &= TC_ST4_VBUS;
	M.ext_vbus = 0;
	if (what == P_HUB) {
		M.r[TC_STATUS_1] = TC_RP_DEFAULT;
		M.r[TC_STATUS_1 + 3] |= TC_ST4_ATTACHED | TC_ST4_DEBOUNCED;
		M.ext_vbus = 1;
	} else if (what == P_ADAPTER) {
		M.r[TC_STATUS_1 + 1] = TC_DFP_RD_OPEN;
		M.r[TC_STATUS_1 + 3] |= TC_ST4_ATTACHED | TC_ST4_DEBOUNCED |
		    TC_ST4_DFP;
	}
	vbus_update();
}

/* The charger as the boot loader leaves it (the tablet, step 1). */
static void
model_reset(unsigned load_ma)
{
	memset(&M, 0, sizeof(M));
	M.load_ma = load_ma;
	M.r[TC_SW_CTRL] = 0x24;		/* sink only */
	M.r[TC_OTG_CFG] = 0x28;
	M.r[TC_DC_SSUPPLY_CFG2] = 0xc0;
	M.r[TC_OTG_RT_STS] = 0x40;
	plug(P_NONE);
}

static unsigned
writes_to(unsigned off)
{
	unsigned i, n = 0;

	for (i = 0; i < M.nlog && i < MLOG; i++)
		if (M.log[i].off == off)
			n++;
	return n;
}

static unsigned
writes_of(unsigned off, uint8_t v)
{
	unsigned i, n = 0;

	for (i = 0; i < M.nlog && i < MLOG; i++)
		if (M.log[i].off == off && M.log[i].v == v)
			n++;
	return n;
}

/* Index of the first write of v to off at or after from, or -1. */
static int
write_at(unsigned from, unsigned off, uint8_t v)
{
	unsigned i;

	for (i = from; i < M.nlog && i < MLOG; i++)
		if (M.log[i].off == off && M.log[i].v == v)
			return (int)i;
	return -1;
}

static int
poll(struct typec_otg *o)
{
	struct typec_state s;

	if (typec_read(&io, &s) != 0)
		return -9;
	return typec_otg_poll(&io, o, &s);
}

static void
fresh(struct typec_otg *o, int pm660)
{
	memset(o, 0, sizeof(*o));
	o->pm660 = pm660;
}

/*===========================================================================*
 *    scenarios                                                              *
 *===========================================================================*/
static void
test_off(void)
{
	struct typec_otg o;
	int i;

	printf("the policy off: nothing written, whatever is plugged in\n");
	model_reset(250);
	fresh(&o, 1);
	plug(P_ADAPTER);
	for (i = 0; i < 5; i++)
		(void)poll(&o);
	plug(P_HUB);
	(void)poll(&o);
	CHECK(M.nlog == 0, "%u writes with the policy off", M.nlog);
	CHECK(!o.boost, "a boost with the policy off");
}

static void
test_hub_cable(void)
{
	struct typec_otg o;
	unsigned n;
	int i;

	printf("auto, the hub's cable: dual role, slow soft start, no more\n");
	model_reset(250);
	fresh(&o, 1);
	plug(P_HUB);
	CHECK(typec_otg_set(&io, &o, TC_OTG_AUTO) == 0, "auto refused: %s",
	    o.last);
	CHECK((M.r[TC_SW_CTRL] & TC_ROLE_MASK) == TC_ROLE_DRP &&
	    (M.r[TC_SW_CTRL] & ~TC_ROLE_MASK) == 0x20,
	    "role register 0x%02x, want dual role with the rest kept",
	    M.r[TC_SW_CTRL]);
	CHECK((M.r[TC_DC_SSUPPLY_CFG2] & 7) == 3 &&
	    (M.r[TC_DC_SSUPPLY_CFG2] & ~7) == 0xc0,
	    "soft start register 0x%02x", M.r[TC_DC_SSUPPLY_CFG2]);
	CHECK(strstr(o.last, "dual role (0x1368: 0x24 -> 0x20)") != NULL,
	    "said: %s", o.last);
	n = M.nlog;
	for (i = 0; i < 5; i++)
		CHECK(poll(&o) == 0, "the hub's cable made the policy act");
	CHECK(M.nlog == n && !o.boost, "%u writes with the hub's cable",
	    M.nlog - n);
	CHECK(M.errors == 0, "model errors: %u", M.errors);
}

static void
test_adapter(unsigned load_ma, unsigned want_step)
{
	struct typec_otg o;
	struct typec_state s;
	int a, b, c;
	unsigned n;

	printf("auto, an OTG adapter whose load starts at %u mA\n", load_ma);
	model_reset(load_ma);
	fresh(&o, 1);
	(void)typec_otg_set(&io, &o, TC_OTG_AUTO);
	n = M.nlog;
	plug(P_ADAPTER);
	CHECK(poll(&o) == 0 && M.nlog == n, "acted on the first sight");
	CHECK(poll(&o) == 1 && o.boost, "no boost on the second: %s", o.last);
	CHECK(o.step_ma == want_step, "soft start at %u mA, want %u",
	    o.step_ma, want_step);
	CHECK(M.r[TC_OTG_CL] == 1, "limit code %u, want 1 (500 mA)",
	    M.r[TC_OTG_CL]);
	CHECK(M.r[TC_USBIN_CMD_IL] & 1, "USBIN not asleep under the boost");
	CHECK(M.r[TC_OTG_ENG] & 1, "the halt mode not set");
	/* The order: USBIN asleep, halt mode, then the enable. */
	a = write_at(n, TC_USBIN_CMD_IL, 0x01);
	b = write_at(n, TC_OTG_ENG, 0x01);
	c = write_at(n, TC_CMD_OTG, 0x01);
	CHECK(a >= 0 && b > a && c > b, "order: USBIN %d, halt %d, enable %d",
	    a, b, c);
	CHECK(write_at(n, TC_OTG_CL, 0) >= 0 && write_at(n, TC_OTG_CL, 0) < c,
	    "the first step is not 250 mA before the enable");
	(void)typec_read(&io, &s);
	CHECK(typec_vbus(&s) == TV_BOOST, "vbus reads %s",
	    typec_vbus_name(typec_vbus(&s)));
	n = M.nlog;
	CHECK(poll(&o) == 0 && M.nlog == n, "a steady state wrote");

	/* Unplugged: everything off, in the order the Android driver uses. */
	plug(P_NONE);
	M.r[TC_OTG_STATUS] = 0x0a;	/* still on until told */
	CHECK(poll(&o) == 1 && !o.boost, "still boosting with nothing there");
	a = write_at(n, TC_OTG_CL, 0);
	b = write_at(n, TC_CMD_OTG, 0);
	c = write_at(n, TC_OTG_ENG, 0);
	CHECK(a >= 0 && b > a && c > b &&
	    write_at(n, TC_USBIN_CMD_IL, 0) > c,
	    "off order: limit %d, enable %d, halt %d", a, b, c);
	CHECK(!(M.r[TC_USBIN_CMD_IL] & 1), "USBIN left asleep");
	CHECK(M.errors == 0, "model errors: %u", M.errors);
}

static void
test_never(void)
{
	struct typec_otg o;
	unsigned n;
	int i;

	printf("auto, a soft start that never finishes\n");
	model_reset(5000);
	fresh(&o, 1);
	(void)typec_otg_set(&io, &o, TC_OTG_AUTO);
	plug(P_ADAPTER);
	(void)poll(&o);
	n = M.nlog;
	CHECK(poll(&o) == -1 && !o.boost && o.fault, "said: %s", o.last);
	CHECK(writes_of(TC_CMD_OTG, 1) == 4,
	    "%u enables, want one at each of the four steps",
	    writes_of(TC_CMD_OTG, 1));
	CHECK(write_at(n, TC_OTG_CL, 5) >= 0, "the 1500 mA step never tried");
	CHECK(M.r[TC_CMD_OTG] == 0 && !(M.r[TC_USBIN_CMD_IL] & 1) &&
	    !(M.r[TC_OTG_ENG] & 1), "left something on");
	n = M.nlog;
	for (i = 0; i < 5; i++)
		(void)poll(&o);
	CHECK(M.nlog == n, "tried again without an unplug (%u writes)",
	    M.nlog - n);
	plug(P_NONE);
	(void)poll(&o);
	CHECK(!o.fault, "the unplug did not clear the fault");
	M.load_ma = 250;
	plug(P_ADAPTER);
	(void)poll(&o);
	CHECK(poll(&o) == 1 && o.boost, "a new device not tried: %s", o.last);
	CHECK(M.errors == 0, "model errors: %u", M.errors);
}

static void
test_overload(void)
{
	struct typec_otg o;
	unsigned n;
	int i;

	printf("auto, the boost overloaded\n");
	model_reset(250);
	fresh(&o, 1);
	(void)typec_otg_set(&io, &o, TC_OTG_AUTO);
	plug(P_ADAPTER);
	(void)poll(&o);
	(void)poll(&o);
	CHECK(o.boost, "no boost");
	M.r[TC_OTG_RT_STS] |= 0x02;	/* overcurrent */
	CHECK(poll(&o) == 1 && !o.boost && o.fault, "said: %s", o.last);
	CHECK(M.r[TC_CMD_OTG] == 0, "the enable left on");
	M.r[TC_OTG_RT_STS] &= ~0x02;
	n = M.nlog;
	for (i = 0; i < 5; i++)
		(void)poll(&o);
	CHECK(M.nlog == n && !o.boost, "restarted without an unplug");
	CHECK(M.errors == 0, "model errors: %u", M.errors);
}

static void
test_external_vbus(void)
{
	struct typec_otg o;
	unsigned n;
	int i;

	printf("auto, a sink and VBUS already there\n");
	model_reset(250);
	fresh(&o, 1);
	(void)typec_otg_set(&io, &o, TC_OTG_AUTO);
	plug(P_ADAPTER);
	M.ext_vbus = 1;
	vbus_update();
	n = M.nlog;
	for (i = 0; i < 5; i++)
		(void)poll(&o);
	CHECK(M.nlog == n && !o.boost, "boosted against VBUS from outside");
	CHECK(strstr(o.last, "already there") != NULL, "said: %s", o.last);
}

static void
test_back_off(void)
{
	struct typec_otg o;

	printf("auto, then off: the boost off and the role put back\n");
	model_reset(250);
	fresh(&o, 1);
	(void)typec_otg_set(&io, &o, TC_OTG_AUTO);
	plug(P_ADAPTER);
	(void)poll(&o);
	(void)poll(&o);
	CHECK(o.boost, "no boost");
	CHECK(typec_otg_set(&io, &o, TC_OTG_OFF) == 0, "off refused");
	CHECK(!o.boost && M.r[TC_CMD_OTG] == 0 &&
	    !(M.r[TC_USBIN_CMD_IL] & 1), "the boost left on");
	CHECK(M.r[TC_SW_CTRL] == 0x24, "role register 0x%02x, want 0x24 back",
	    M.r[TC_SW_CTRL]);
	CHECK(M.errors == 0, "model errors: %u", M.errors);
}

static void
test_pmi8998(void)
{
	struct typec_otg o;

	printf("the PMI8998: no steps\n");
	model_reset(250);
	fresh(&o, 0);
	(void)typec_otg_set(&io, &o, TC_OTG_AUTO);
	plug(P_ADAPTER);
	(void)poll(&o);
	CHECK(poll(&o) == 1 && o.boost, "said: %s", o.last);
	CHECK(writes_to(TC_OTG_CL) == 1 && M.r[TC_OTG_CL] == 1,
	    "%u limit writes, want only the limit", writes_to(TC_OTG_CL));
}

int
main(void)
{
	test_off();
	test_hub_cable();
	test_adapter(250, 250);
	test_adapter(900, 1000);
	test_never();
	test_overload();
	test_external_vbus();
	test_back_off();
	test_pmi8998();
	printf("otgtest: %u checks, %u failed\n", checks, failures);
	return failures != 0;
}
