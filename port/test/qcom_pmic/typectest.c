/*
 * Host test of the PMIC driver's Type-C reading (qcom_pmic/typec.c).
 *
 * The charger is a byte array the size of its peripherals (0x1000 bytes
 * from its base); reads come from it, writes are counted and refused.
 * Checked, against the register meanings of the Mi Pad 4's own Android
 * charger driver (port/clover/hw-usbhost.md, section 3):
 *
 *   - who is at the other end, for every status the charger can report:
 *     each Rp as a source (with its current), Rd and Rd-through-a-powered-
 *     cable as a sink, Ra/Ra and Rd/Rd as the accessories they are, an
 *     Rp byte with anything else in it and a source-side nibble with no
 *     known value as unknown when attached and none when not;
 *   - where VBUS comes from: our boost (by the OTG block's state or by
 *     CMD_OTG) before USBIN's plug-in bit, which our own boost raises too;
 *   - the texts the driver prints, on the registers a boot loader leaves;
 *   - that none of it writes a single register.
 *
 * With the Mi Pad 4's tree (the first argument; GPL-2.0-only, not kept
 * here), the charger node is found where the tree puts it: the PM660 at
 * USID 0, reg 0x1000, with the PM660's OTG work-around.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <minix/fdt.h>

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

static uint8_t chg[0x1000];
static unsigned writes, reads;

static int
m_rd(void *ctx, unsigned off, uint8_t *buf, unsigned n)
{
	(void)ctx;
	if (n == 0 || n > 8 || off + n > sizeof(chg))
		return -1;
	memcpy(buf, chg + off, n);
	reads++;
	return 0;
}

static int
m_wr(void *ctx, unsigned off, uint8_t v)
{
	(void)ctx;
	(void)off;
	(void)v;
	writes++;
	return -1;
}

static int
m_writable(void *ctx, unsigned off)
{
	(void)ctx;
	return off >= 0x100 && off < 0x400;	/* OTG, BATIF, USBIN */
}

static void
m_delay(void *ctx, unsigned us)
{
	(void)ctx;
	(void)us;
}

static struct typec_io io = {
	.rd = m_rd, .wr = m_wr, .writable = m_writable, .delay_us = m_delay,
	.base = 0x1000
};

/* Status bytes 1..5, USBIN's plug-in bit, OTG status, CMD_OTG. */
static void
set(uint8_t s1, uint8_t s2, uint8_t s4, int plugin, uint8_t otg_st,
	uint8_t cmd)
{
	memset(chg, 0, sizeof(chg));
	chg[TC_STATUS_1] = s1;
	chg[TC_STATUS_1 + 1] = s2;
	chg[TC_STATUS_1 + 3] = s4;
	chg[TC_USBIN_RT_STS] = plugin ? TC_USBIN_PLUGIN : 0;
	chg[TC_OTG_STATUS] = otg_st;
	chg[TC_CMD_OTG] = cmd;
}

static void
partner_case(const char *what, uint8_t s1, uint8_t s2, uint8_t s4,
	enum typec_partner want, unsigned want_ma)
{
	struct typec_state s;

	set(s1, s2, s4, 0, 0, 0);
	CHECK(typec_read(&io, &s) == 0, "%s: read failed", what);
	CHECK(typec_partner(&s) == want, "%s: partner %s, want %s", what,
	    typec_partner_name(typec_partner(&s)), typec_partner_name(want));
	CHECK(typec_rp_ma(&s) == want_ma, "%s: %u mA, want %u", what,
	    typec_rp_ma(&s), want_ma);
}

static void
test_partners(void)
{
	const uint8_t att = TC_ST4_ATTACHED | TC_ST4_DEBOUNCED;
	const uint8_t dfp = att | TC_ST4_DFP;

	printf("who is at the other end\n");
	partner_case("nothing", 0x01, 0, 0, TC_NONE, 0);
	partner_case("hub cable, Rp default", TC_RP_DEFAULT, 0, att | 0x40,
	    TC_SOURCE, 500);
	partner_case("charger, Rp 1.5 A", TC_RP_1P5A, 0, att, TC_SOURCE, 1500);
	partner_case("charger, Rp 3 A", TC_RP_3A, 0, att, TC_SOURCE, 3000);
	/* Android compares the whole byte: Rp and a factory-mode bit. */
	partner_case("Rp with another bit", TC_RP_DEFAULT | 0x10, 0, att,
	    TC_UNKNOWN, 0);
	partner_case("Rp byte, not attached", TC_RP_DEFAULT | 0x10, 0, 0,
	    TC_NONE, 0);
	partner_case("OTG adapter, Rd", 0, TC_DFP_RD_OPEN, dfp, TC_SINK, 0);
	partner_case("powered cable, Rd+Ra", 0, TC_DFP_RD_RA_VCONN, dfp,
	    TC_SINK, 0);
	/* The upper bits of STATUS_2 are not the CC pins. */
	partner_case("Rd with timer bits", 0, 0x40 | 0x20 | TC_DFP_RD_OPEN,
	    dfp, TC_SINK, 0);
	partner_case("audio, Ra/Ra", 0, TC_DFP_RA_RA, dfp, TC_AUDIO, 0);
	partner_case("debug, Rd/Rd", 0, TC_DFP_RD_RD, dfp, TC_DEBUG, 0);
	partner_case("source side, nothing known", 0, 0x03, dfp, TC_UNKNOWN,
	    0);
	/* The DFP bit decides which byte counts, not the other way round. */
	partner_case("Rd byte while a sink", TC_RP_DEFAULT, TC_DFP_RD_OPEN, att,
	    TC_SOURCE, 500);
	partner_case("Rp byte while a source", TC_RP_DEFAULT, TC_DFP_RD_OPEN,
	    dfp, TC_SINK, 0);
}

static void
vbus_case(const char *what, int plugin, uint8_t otg_st, uint8_t cmd,
	enum typec_vbus want)
{
	struct typec_state s;

	set(0, 0, 0, plugin, otg_st, cmd);
	(void)typec_read(&io, &s);
	CHECK(typec_vbus(&s) == want, "%s: vbus %s, want %s", what,
	    typec_vbus_name(typec_vbus(&s)), typec_vbus_name(want));
}

static void
test_vbus(void)
{
	printf("where VBUS comes from\n");
	vbus_case("none", 0, 0, 0, TV_NONE);
	vbus_case("the hub's", 1, 0, 0, TV_EXTERNAL);
	vbus_case("our boost, and it raises plug-in", 1, 0x0a, 0x01,
	    TV_BOOST);
	vbus_case("our boost by its state alone", 1, 0x02, 0, TV_BOOST);
	vbus_case("asked for, not up yet", 0, 0x01, 0x01, TV_BOOST);
	vbus_case("soft start bit without the state", 1, 0x08, 0,
	    TV_EXTERNAL);
}

static int
contains(const char *s, const char *what)
{
	return strstr(s, what) != NULL;
}

static void
test_texts(void)
{
	struct typec_state s;
	char line[512];

	printf("what the driver prints\n");
	/* The hub's cable as a boot loader might leave it. */
	set(TC_RP_DEFAULT, 0, TC_ST4_ATTACHED | TC_ST4_VBUS, 1, 0, 0);
	chg[TC_SW_CTRL] = 0x10 | TC_ROLE_UFP;
	chg[TC_OTG_CFG] = 0x00;
	chg[TC_OTG_CL] = 0x05;
	(void)typec_read(&io, &s);
	(void)typec_describe(&s, line, sizeof(line));
	CHECK(contains(line, "type-c source") && contains(line, "500 mA") &&
	    contains(line, "vbus external"), "describe: \"%s\"", line);
	(void)typec_config_text(&io, line, sizeof(line));
	CHECK(contains(line, "Type-C port") && contains(line, "sink only") &&
	    contains(line, "0x1368 = 0x14") && contains(line, "by software") &&
	    contains(line, "boost off") && contains(line, "1500 mA") &&
	    contains(line, "OTG block ours"), "config: \"%s\"", line);
	chg[TC_SW_CTRL] = 0x10;
	chg[TC_OTG_CFG] = 0x02;
	chg[TC_OTG_RID_CC] = 0x20;
	(void)typec_config_text(&io, line, sizeof(line));
	CHECK(contains(line, "micro-USB port") && contains(line, "dual role") &&
	    contains(line, "VBUS by Type-C"), "config: \"%s\"", line);

	set(0, TC_DFP_RD_OPEN, TC_ST4_ATTACHED | TC_ST4_DFP, 0, 0, 0);
	(void)typec_read(&io, &s);
	(void)typec_describe(&s, line, sizeof(line));
	CHECK(contains(line, "type-c sink (Rd") && contains(line, "host") &&
	    contains(line, "vbus none"), "describe: \"%s\"", line);

	/* The dump names absolute addresses and every register once. */
	memset(chg, 0, sizeof(chg));
	chg[TC_SW_CTRL] = 0xa4;
	chg[TC_FREQ_CLK_DIV] = 0x0b;
	(void)typec_dump(&io, line, sizeof(line));
	CHECK(contains(line, "1368:a4") && contains(line, "1950:0b") &&
	    contains(line, "130b:00") && contains(line, "130f:00") &&
	    contains(line, "1108:00") && contains(line, "14c1:00"),
	    "dump: \"%s\"", line);
}

static void
test_find(const char *path)
{
	struct typec_chg c;
	FILE *f;
	long size;
	void *dtb;

	printf("the charger in %s\n", path);
	if ((f = fopen(path, "rb")) == NULL) {
		CHECK(0, "cannot open %s", path);
		return;
	}
	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	dtb = malloc((size_t)size);
	if (dtb == NULL || fread(dtb, 1, (size_t)size, f) != (size_t)size) {
		CHECK(0, "cannot read %s", path);
		fclose(f);
		free(dtb);
		return;
	}
	fclose(f);
	CHECK(typec_find(dtb, &c) == 0, "no charger found");
	CHECK(c.sid == 0 && c.base == 0x1000 && c.pm660 &&
	    strcmp(c.compat, "qcom,pm660-charger") == 0,
	    "charger: SID %u base 0x%x pm660 %d \"%s\"", c.sid, c.base,
	    c.pm660, c.compat);
	free(dtb);
}

int
main(int argc, char **argv)
{
	test_partners();
	test_vbus();
	test_texts();
	if (argc > 1)
		test_find(argv[1]);
	CHECK(writes == 0, "%u register write(s); reading must write none",
	    writes);
	printf("typectest: %u checks, %u failed (%u reads)\n", checks,
	    failures, reads);
	return failures != 0;
}
