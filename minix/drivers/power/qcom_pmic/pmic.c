/*
 * qcom_pmic: the buttons, the backlight and the battery of a Qualcomm
 * PM660/PM660L pair, reached over the SPMI PMIC arbiter (spmi.c).
 *
 * Found by the arbiter's compatible, "qcom,spmi-pmic-arb"; a machine
 * without one (QEMU's virt, the CB2) gets a line saying so and nothing
 * else.  Everything is polled - twenty times a second for the buttons, once
 * a minute for the battery - so no PMIC interrupt is configured and the
 * arbiter's interrupt region is never touched.
 *
 * What it does with them (registers: port/clover/hw-pmic.md):
 *
 *   Volume up / down   the arrow keys up / down, to the input server - the
 *                      way a keyboard's arrows reach tty: shell history,
 *                      and something to press with no keyboard at all.
 *   Power              the backlight off, and on again.  Any key wakes it.
 *   Battery            charge, voltage, current and charger state, printed
 *                      when the state changes, every 10% and every ten
 *                      minutes; warnings at 15% and 5%.  A charger watchdog
 *                      the boot loader left armed is fed, or charging stops.
 *   Next boot          where the boot loader goes after the next reset:
 *                      "bootloader" (fastboot), "recovery" or "normal", from
 *                      the boot argument nextboot= or, as root, sysctl -w
 *                      minix.pmic.nextboot=... - so that whoever reaches
 *                      the machine over the network can bring it back to
 *                      fastboot without anyone at its buttons.
 *   Type-C             who is at the other end of the USB-C cable and where
 *                      VBUS comes from (typec.c), five times a second, as
 *                      sysctl minix.pmic.typec and minix.pmic.vbus and a
 *                      line when either changes - what decides whether the
 *                      port is a device or a host (port/clover/
 *                      hw-usbhost.md).  The registers that govern a role
 *                      and VBUS are printed as the boot loader left them.
 *
 * It writes as little as it can: the volume-up pin's input configuration
 * once, the backlight's module enable, the charger watchdog's pet, and the
 * PON's soft reboot spare register when asked.  Each write goes only to a
 * peripheral this EE owns (spmi_write()).  It never writes the PON reset
 * timers, the PS_HOLD reset type, the regulators, or the charger's
 * settings.
 *
 * Powering off properly would need the PMIC told what to do when PS_HOLD
 * drops (hw-pmic.md, section 5) before PSCI's SYSTEM_OFF; that is not done
 * yet, so /sbin/poweroff may reset instead.
 */

#include <minix/drivers.h>
#include <minix/inputdriver.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/osk.h>
#include <minix/rmib.h>
#include <minix/sysctl.h>
#include <minix/sysutil.h>
#include <minix/vm.h>

#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#include "spmi.h"
#include "typec.h"

static struct log pmic_log = {
	.name = "pmic",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/* PM660 (SID 0) and PM660L (SIDs 2 and 3), hw-pmic.md. */
#define SID_PM660		0
#define SID_PM660L_A		2
#define SID_PM660L_B		3

#define PON_REV2		0x0801	/* 0: PS_HOLD enable in RST_CTL */
#define PON_RT_STS		0x0810	/* bit0 power, bit1 volume down */
#define PON_KPDPWR		0x01
#define PON_RESIN		0x02
#define PON_PS_HOLD_RST_CTL	0x085a	/* bits 3:0 what a PS_HOLD drop does */
#define PON_PS_HOLD_RST_CTL2	0x085b	/* bit7 enable, if PON_REV2 != 0 */
#define PON_SOFT_RB_SPARE	0x088f	/* bits 7:1 the boot loader's mode */
#define PON_REASON_SHIFT	1	/* gen2, as qcom,pm8998-pon */

#define VOLUP_BASE		0xc600	/* PM660L GPIO7 */
#define GPIO_TYPE		0x04
#define GPIO_RT_STS		0x10
#define GPIO_MODE_CTL		0x40
#define GPIO_PULL_CTL		0x42
#define GPIO_IN_CTL		0x43
#define GPIO_EN_CTL		0x46
#define GPIO_TYPE_GPIO		0x10

#define WLED_MOD_EN		0xd846	/* bit7: module on */
#define WLED_SYNC		0xd947
#define WLED_BRIGHT(n)		(0xd957 + 0x10 * (n))
#define WLED_STRINGS		3	/* clover: qcom,num-strings */
#define WLED_MIN		16
#define WLED_MAX		4095

#define CHG_STATUS1		0x1006	/* bits 2:0 */
#define CHG_POWER_PATH		0x160b	/* bit4 USE_USBIN, bit0 valid input */
#define CHG_WD_CFG		0x1651	/* bit0 watchdog on */
#define CHG_WD_PET		0x1643
#define FG_SOC			0x4009	/* two bytes, take the smaller */
#define FG_VOLTAGE		0x41a0	/* u16, 122.07 uV */
#define FG_CURRENT		0x41a2	/* s16, 488.28 uA, + is discharge */

#define POLL_TICKS_HZ		20	/* button polls a second */
#define BATT_EVERY		(60 * POLL_TICKS_HZ)
#define BATT_REPORT_EVERY	10	/* minutes between plain reports */
#define WD_PET_EVERY		(10 * POLL_TICKS_HZ)
#define LDO_REPORT_AGAIN	(10 * POLL_TICKS_HZ)	/* after qcom_rpm */
#define TYPEC_EVERY		4	/* polls between Type-C reads */

static void ldo_report(void);

static struct spmi spmi;
static int have_pon, have_volup, have_wled, have_fg, have_chg;
static int wled_on = 1;
static unsigned wled_level = 2048;
static unsigned keys_down;		/* bit0 power, 1 vol-, 2 vol+ */
static unsigned long polls;
static int wd_armed;
static struct { int valid, soc, charging, online; unsigned minutes; } last;

static void
delay_us(unsigned us)
{
	(void)micro_delay(us);
}

static int
rd8(unsigned sid, unsigned addr, uint8_t *v)
{
	return spmi_read(&spmi, sid, addr, v, 1);
}

static int
wr8(unsigned sid, unsigned addr, uint8_t v)
{
	return spmi_write(&spmi, sid, addr, &v, 1);
}

/*===========================================================================*
 *    the arbiter, from the device tree                                       *
 *===========================================================================*/
struct find {
	int found;
	u64_t addr[4], size[4];		/* core, chnls, obsrvr, cnfg */
	unsigned ee;
};

static const char *const regions[4] = { "core", "chnls", "obsrvr", "cnfg" };

static int
find_arb(void *cookie, int UNUSED(depth), const char *UNUSED(name),
	const struct fdt_node *node)
{
	struct find *f = cookie;
	const char *names, *p;
	unsigned len, idx, i;
	u64_t a, sz;

	if (!fdt_node_is_compatible(node, "qcom,spmi-pmic-arb"))
		return 0;
	if ((names = fdt_getprop(node, "reg-names", &len)) == NULL)
		return 0;
	for (p = names, idx = 0; p < names + len; p += strlen(p) + 1, idx++) {
		for (i = 0; i < 4; i++)
			if (strcmp(p, regions[i]) == 0 &&
			    fdt_node_reg(node, idx, &a, &sz) == 0) {
				f->addr[i] = a;
				f->size[i] = sz;
			}
	}
	f->ee = 0;
	if ((p = fdt_getprop(node, "qcom,ee", &len)) != NULL && len == 4)
		f->ee = (unsigned)fdt_read_cells(p, 1);
	for (i = 0; i < 4; i++)
		if (f->size[i] == 0)
			return 0;
	f->found = 1;
	return 1;
}

static volatile uint8_t *
map_region(u64_t pa, u64_t size, const char *what)
{
	void *v;

	if ((v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)size)) ==
	    MAP_FAILED) {
		log_warn(&pmic_log, "cannot map the arbiter's %s at 0x%llx; "
		    "not granted by RS?\n", what, (unsigned long long)pa);
		return NULL;
	}
	return v;
}

static int
arb_init(void)
{
	struct find f;
	void *dtb;
	int r;

	if ((dtb = fdt_fetch()) == NULL) {
		log_warn(&pmic_log, "no device tree\n");
		return ENXIO;
	}
	memset(&f, 0, sizeof(f));
	fdt_walk(dtb, find_arb, &f);
	free(dtb);
	if (!f.found) {
		log_info(&pmic_log, "no SPMI PMIC arbiter in the tree; nothing "
		    "to do\n");
		return ENXIO;
	}

	memset(&spmi, 0, sizeof(spmi));
	if ((spmi.core = map_region(f.addr[0], f.size[0], "core")) == NULL ||
	    (spmi.chnls = map_region(f.addr[1], f.size[1], "channels")) ==
	    NULL ||
	    (spmi.obsrvr = map_region(f.addr[2], f.size[2], "observer")) ==
	    NULL ||
	    (spmi.cnfg = map_region(f.addr[3], f.size[3], "config")) == NULL)
		return EPERM;
	spmi.core_size = (size_t)f.size[0];
	spmi.chnls_size = (size_t)f.size[1];
	spmi.obsrvr_size = (size_t)f.size[2];
	spmi.cnfg_size = (size_t)f.size[3];
	spmi.ee = f.ee;
	spmi.delay_us = delay_us;

	if ((r = spmi_init(&spmi)) != OK_SPMI) {
		log_warn(&pmic_log, "arbiter version 0x%08x: not v2/v3, which "
		    "is all this driver knows; leaving it alone\n",
		    spmi.version);
		return r;
	}
	log_info(&pmic_log, "SPMI arbiter v%u (0x%08x), EE %u, %u channels\n",
	    spmi.version >= 0x30000000u ? 3 : 2, spmi.version, spmi.ee,
	    spmi.napid);
	return OK;
}

/*===========================================================================*
 *    buttons and backlight                                                   *
 *===========================================================================*/
static void
key(unsigned usage, int down)
{
	inputdriver_send_event(FALSE, INPUT_PAGE_KEY, usage,
	    down ? INPUT_PRESS : INPUT_RELEASE, 0);
}

static void
wled_set(unsigned b)
{
	uint8_t v[2], s;
	unsigned n;

	if (b < WLED_MIN)
		b = WLED_MIN;
	if (b > WLED_MAX)
		b = WLED_MAX;
	v[0] = (uint8_t)(b & 0xff);
	v[1] = (uint8_t)(b >> 8);
	for (n = 0; n < WLED_STRINGS; n++)
		(void)spmi_write(&spmi, SID_PM660L_B, WLED_BRIGHT(n), v, 2);
	if (rd8(SID_PM660L_B, WLED_SYNC, &s) == OK_SPMI) {
		(void)wr8(SID_PM660L_B, WLED_SYNC, s & ~0x0f);
		(void)wr8(SID_PM660L_B, WLED_SYNC, s | 0x0f);
	}
}

static void
wled_power(int on)
{
	uint8_t m;

	if (!have_wled || rd8(SID_PM660L_B, WLED_MOD_EN, &m) != OK_SPMI)
		return;
	if (on) {
		wled_set(wled_level);
		(void)wr8(SID_PM660L_B, WLED_MOD_EN, m | 0x80);
	} else
		(void)wr8(SID_PM660L_B, WLED_MOD_EN, m & ~0x80);
	wled_on = on;
	log_debug(&pmic_log, "backlight %s\n", on ? "on" : "off");
}

static void
buttons_init(void)
{
	uint8_t v, lo, hi;

	have_pon = rd8(SID_PM660, PON_RT_STS, &v) == OK_SPMI;

	/* Volume up: PM660L GPIO7 as an input with its 30 uA pull-up. */
	if (rd8(SID_PM660L_A, VOLUP_BASE + GPIO_TYPE, &v) == OK_SPMI &&
	    v == GPIO_TYPE_GPIO) {
		if (spmi_writable(&spmi, SID_PM660L_A, VOLUP_BASE)) {
			(void)wr8(SID_PM660L_A, VOLUP_BASE + GPIO_PULL_CTL, 0);
			(void)wr8(SID_PM660L_A, VOLUP_BASE + GPIO_IN_CTL, 0);
			(void)wr8(SID_PM660L_A, VOLUP_BASE + GPIO_MODE_CTL, 0);
			(void)wr8(SID_PM660L_A, VOLUP_BASE + GPIO_EN_CTL,
			    0x80);
		}
		have_volup = 1;
	}

	/* The backlight: as the boot loader left it, and its level. */
	if (spmi_writable(&spmi, SID_PM660L_B, WLED_MOD_EN) &&
	    rd8(SID_PM660L_B, WLED_MOD_EN, &v) == OK_SPMI) {
		have_wled = 1;
		wled_on = (v & 0x80) != 0;
		if (rd8(SID_PM660L_B, WLED_BRIGHT(0), &lo) == OK_SPMI &&
		    rd8(SID_PM660L_B, WLED_BRIGHT(0) + 1, &hi) == OK_SPMI &&
		    ((hi << 8 | lo) & 0xfff) >= WLED_MIN)
			wled_level = (hi << 8 | lo) & 0xfff;
	}

	log_info(&pmic_log, "power key %s, volume down %s, volume up %s, "
	    "backlight %s (level %u, %s)\n", have_pon ? "yes" : "no",
	    have_pon ? "yes" : "no", have_volup ? "yes" : "no",
	    have_wled ? "yes" : "no", wled_level, wled_on ? "on" : "off");
}

static void
buttons_poll(void)
{
	uint8_t pon = 0, gpio = 1;
	unsigned now = 0, changed, i;
	static const unsigned usage[3] = { 0, OSK_U_DOWN, OSK_U_UP };

	if (have_pon && rd8(SID_PM660, PON_RT_STS, &pon) == OK_SPMI) {
		if (pon & PON_KPDPWR)
			now |= 1;
		if (pon & PON_RESIN)
			now |= 2;
	}
	if (have_volup && rd8(SID_PM660L_A, VOLUP_BASE + GPIO_RT_STS,
	    &gpio) == OK_SPMI && !(gpio & 1))
		now |= 4;

	changed = now ^ keys_down;
	keys_down = now;
	if (changed == 0)
		return;

	/* A key pressed with the screen dark only lights it. */
	if (!wled_on && have_wled && (changed & now)) {
		wled_power(1);
		return;
	}
	if ((changed & 1) && (now & 1))
		wled_power(0);
	for (i = 1; i < 3; i++)
		if (changed & (1u << i))
			key(usage[i], (now >> i) & 1);
}

/*===========================================================================*
 *    battery                                                                 *
 *===========================================================================*/
static void
battery_init(void)
{
	uint8_t v;

	have_fg = rd8(SID_PM660, FG_SOC, &v) == OK_SPMI;
	have_chg = rd8(SID_PM660, CHG_STATUS1, &v) == OK_SPMI;
	if (have_chg && rd8(SID_PM660, CHG_WD_CFG, &v) == OK_SPMI &&
	    (v & 1) && spmi_writable(&spmi, SID_PM660, CHG_WD_PET)) {
		wd_armed = 1;
		log_info(&pmic_log, "the charger's watchdog is armed; feeding "
		    "it, or charging would stop\n");
	}
}

static void
battery_poll(int force)
{
	uint8_t s[2], v[2], c[2], st = 0, path = 0;
	int soc = -1, charging = 0, online = 0, mv = 0, ma = 0;
	unsigned raw;

	if (!have_fg && !have_chg)
		return;
	if (have_fg && spmi_read(&spmi, SID_PM660, FG_SOC, s, 2) == OK_SPMI) {
		raw = s[0] < s[1] ? s[0] : s[1];
		soc = raw == 0xff ? 100 : raw == 0 ? 0 :
		    (int)(((raw - 1) * 98 + 126) / 253) + 1;
		if (spmi_read(&spmi, SID_PM660, FG_VOLTAGE, v, 2) == OK_SPMI)
			mv = (int)((unsigned long)(v[0] | v[1] << 8) * 122070 /
			    1000000);
		if (spmi_read(&spmi, SID_PM660, FG_CURRENT, c, 2) == OK_SPMI)
			ma = -(int)((long)(int16_t)(c[0] | c[1] << 8) * 488281 /
			    1000000);
	}
	if (have_chg && rd8(SID_PM660, CHG_POWER_PATH, &path) == OK_SPMI &&
	    rd8(SID_PM660, CHG_STATUS1, &st) == OK_SPMI) {
		online = (path & 0x10) && (path & 0x01);
		charging = online && (st & 7) <= 4;
	}

	if (!force && last.valid && last.charging == charging &&
	    last.online == online && soc / 10 == last.soc / 10 &&
	    ++last.minutes < BATT_REPORT_EVERY)
		return;
	if (soc >= 0 && (!last.valid || soc != last.soc) && !charging) {
		if (soc <= 5)
			log_warn(&pmic_log, "battery critically low (%d%%): "
			    "connect a charger\n", soc);
		else if (soc <= 15)
			log_warn(&pmic_log, "battery low (%d%%)\n", soc);
	}
	log_info(&pmic_log, "battery %d%%, %d.%03d V, %d mA; %s\n", soc,
	    mv / 1000, mv % 1000, ma, !online ? "on battery" :
	    charging ? "charging" : "charger connected, not charging");
	last.valid = 1;
	last.soc = soc;
	last.charging = charging;
	last.online = online;
	last.minutes = 0;
}

/*===========================================================================*
 *    the USB-C port                                                          *
 *===========================================================================*/
static struct typec_chg tchg;
static int have_typec;
static struct typec_state tstate;
static enum typec_partner tpartner = TC_UNKNOWN;
static enum typec_vbus tvbus = TV_NONE;

static int
tc_rd(void *UNUSED(ctx), unsigned off, uint8_t *buf, unsigned n)
{
	return spmi_read(&spmi, tchg.sid, tchg.base + off, buf, n);
}

static int
tc_wr(void *UNUSED(ctx), unsigned off, uint8_t v)
{
	return wr8(tchg.sid, tchg.base + off, v);
}

static int
tc_writable(void *UNUSED(ctx), unsigned off)
{
	return spmi_writable(&spmi, tchg.sid, tchg.base + off);
}

static void
tc_delay(void *UNUSED(ctx), unsigned us)
{
	delay_us(us);
}

static struct typec_io tio = {
	.rd		= tc_rd,
	.wr		= tc_wr,
	.writable	= tc_writable,
	.delay_us	= tc_delay,
};

/*
 * The charger node, and what the registers that decide a role and VBUS
 * hold as the boot loader left them - the record the first run on the
 * machine is for (hw-usbhost.md, step 1).  Nothing is written.
 */
static void
typec_init(void)
{
	char line[512];
	void *dtb;
	int r;

	if ((dtb = fdt_fetch()) == NULL)
		return;
	r = typec_find(dtb, &tchg);
	free(dtb);
	if (r != OK) {
		log_info(&pmic_log, "no SMB2 charger in the tree: no type-c\n");
		return;
	}
	tio.base = tchg.base;
	if (typec_read(&tio, &tstate) != OK_SPMI) {
		log_warn(&pmic_log, "%s at SID %u 0x%04x: type-c status "
		    "unreadable\n", tchg.compat, tchg.sid, tchg.base);
		return;
	}
	have_typec = 1;
	tpartner = typec_partner(&tstate);
	tvbus = typec_vbus(&tstate);
	(void)typec_config_text(&tio, line, sizeof(line));
	log_info(&pmic_log, "%s at SID %u 0x%04x: %s\n", tchg.compat,
	    tchg.sid, tchg.base, line);
	(void)typec_dump(&tio, line, sizeof(line));
	log_info(&pmic_log, "type-c registers: %s\n", line);
	(void)typec_describe(&tstate, line, sizeof(line));
	log_info(&pmic_log, "%s\n", line);
}

static void
typec_poll(void)
{
	char line[160];
	enum typec_partner p;
	enum typec_vbus v;

	if (!have_typec || typec_read(&tio, &tstate) != OK_SPMI)
		return;
	p = typec_partner(&tstate);
	v = typec_vbus(&tstate);
	if (p == tpartner && v == tvbus)
		return;
	tpartner = p;
	tvbus = v;
	(void)typec_describe(&tstate, line, sizeof(line));
	log_info(&pmic_log, "%s\n", line);
}

/* sysctl minix.pmic.typec and minix.pmic.vbus: the last state read. */
static char typec_str[16], vbus_str[16];

static struct rmib_node typec_data = RMIB_STRING(RMIB_RO, typec_str,
	"typec", "What is at the other end of the USB-C cable");
static struct rmib_node vbus_data = RMIB_STRING(RMIB_RO, vbus_str,
	"vbus", "Where VBUS comes from");

static ssize_t
typec_func(struct rmib_call *call, struct rmib_node *UNUSED(node),
	struct rmib_oldp *oldp, struct rmib_newp *newp)
{
	strlcpy(typec_str, have_typec ? typec_partner_name(tpartner) :
	    "unknown", sizeof(typec_str));
	return rmib_readwrite(call, &typec_data, oldp, newp);
}

static ssize_t
vbus_func(struct rmib_call *call, struct rmib_node *UNUSED(node),
	struct rmib_oldp *oldp, struct rmib_newp *newp)
{
	strlcpy(vbus_str, have_typec ? typec_vbus_name(tvbus) : "unknown",
	    sizeof(vbus_str));
	return rmib_readwrite(call, &vbus_data, oldp, newp);
}

/*===========================================================================*
 *    where the next reset goes                                               *
 *===========================================================================*/
/*
 * The boot loader's modes, as the PM660's PON node in the tree numbers them
 * (pm660.dtsi: mode-recovery = <1>, mode-bootloader = <2>), written the way
 * Linux's reboot-mode writes them (qcom-pon.c, gen2): the mode shifted left
 * by one into bits 7:1 of PON_SOFT_RB_SPARE, bit 0 kept.  The register
 * outlives the reset; Qualcomm's ABL reads it and goes to fastboot on 2.
 *
 * Linux also makes a PS_HOLD drop a hard reset before it restarts
 * (pm8941-pwrkey.c), and the mode is meant to survive that.  This driver
 * does not touch the PS_HOLD setting: it prints what the boot loader left,
 * so that one reset on the machine shows whether the mode survives as it
 * is (port/clover/hw-net.md, step 1).
 */
static const struct { const char *name; unsigned mode; } boot_modes[] = {
	{ "normal",	0 },
	{ "recovery",	1 },
	{ "bootloader",	2 },
};

static int have_spare;		/* PON_SOFT_RB_SPARE is ours to write */
static char nextboot_str[16];	/* minix.pmic.nextboot's buffer */

static const char *
mode_name(unsigned mode)
{
	unsigned i;

	for (i = 0; i < __arraycount(boot_modes); i++)
		if (boot_modes[i].mode == mode)
			return boot_modes[i].name;
	return NULL;
}

/* The mode the register holds, by name, or as a number if it has none. */
static void
mode_string(uint8_t spare, char *buf, size_t size)
{
	const char *name;

	if ((name = mode_name(spare >> PON_REASON_SHIFT)) != NULL)
		strlcpy(buf, name, size);
	else
		snprintf(buf, size, "%u", (unsigned)(spare >> PON_REASON_SHIFT));
}

static void
pon_report(void)
{
	uint8_t rev, ctl, en, spare;
	unsigned type;
	char mode[16];

	if (rd8(SID_PM660, PON_REV2, &rev) != OK_SPMI ||
	    rd8(SID_PM660, PON_PS_HOLD_RST_CTL, &ctl) != OK_SPMI ||
	    rd8(SID_PM660, rev == 0 ? PON_PS_HOLD_RST_CTL :
	    PON_PS_HOLD_RST_CTL2, &en) != OK_SPMI ||
	    rd8(SID_PM660, PON_SOFT_RB_SPARE, &spare) != OK_SPMI) {
		log_warn(&pmic_log, "PON: unreadable; no next boot\n");
		return;
	}
	have_spare = spmi_writable(&spmi, SID_PM660, PON_SOFT_RB_SPARE);
	type = ctl & 0x0f;
	mode_string(spare, mode, sizeof(mode));
	log_info(&pmic_log, "PON rev %u: PS_HOLD drop = %s (type %u, %s); "
	    "next boot %s (0x%03x = 0x%02x), %s\n", rev,
	    type == 1 ? "warm reset" : type == 4 ? "shutdown" :
	    type == 7 ? "hard reset" : "?", type,
	    (en & 0x80) ? "enabled" : "disabled", mode, PON_SOFT_RB_SPARE,
	    spare, have_spare ? "writable" : "not this EE's");
}

static int
nextboot_set(const char *name)
{
	uint8_t old, now;
	unsigned i, mode;

	for (i = 0; i < __arraycount(boot_modes); i++)
		if (strcmp(name, boot_modes[i].name) == 0)
			break;
	if (i == __arraycount(boot_modes)) {
		log_warn(&pmic_log, "next boot \"%s\": not normal, recovery or "
		    "bootloader\n", name);
		return EINVAL;
	}
	mode = boot_modes[i].mode;
	if (!have_spare) {
		log_warn(&pmic_log, "next boot %s: the PON is not this EE's to "
		    "write\n", name);
		return EPERM;
	}
	if (rd8(SID_PM660, PON_SOFT_RB_SPARE, &old) != OK_SPMI ||
	    wr8(SID_PM660, PON_SOFT_RB_SPARE, (uint8_t)((old &
	    ((1u << PON_REASON_SHIFT) - 1)) | mode << PON_REASON_SHIFT)) !=
	    OK_SPMI || rd8(SID_PM660, PON_SOFT_RB_SPARE, &now) != OK_SPMI) {
		log_warn(&pmic_log, "next boot %s: the PON did not take it\n",
		    name);
		return EIO;
	}
	if ((unsigned)(now >> PON_REASON_SHIFT) != mode) {
		log_warn(&pmic_log, "next boot %s: wrote it, reads back 0x%02x\n",
		    name, now);
		return EIO;
	}
	log_info(&pmic_log, "next boot: %s (0x%03x: 0x%02x -> 0x%02x)\n", name,
	    PON_SOFT_RB_SPARE, old, now);
	return OK;
}

/*
 * sysctl minix.pmic.nextboot: reading gives the register's mode now,
 * writing one of the names sets it.  Writing takes root, which the remote
 * MIB library checks before this is called.
 */
static ssize_t nextboot_func(struct rmib_call *, struct rmib_node *,
	struct rmib_oldp *, struct rmib_newp *);

static struct rmib_node nextboot_data = RMIB_STRING(RMIB_RW, nextboot_str,
	"nextboot", "Where the boot loader goes after the next reset");

static struct rmib_node pmic_table[] = {
/* 0*/	[0]	= RMIB_FUNC(CTLTYPE_STRING | RMIB_RW, sizeof(nextboot_str),
		    nextboot_func, "nextboot", "Where the boot loader goes "
		    "after the next reset: normal, recovery or bootloader"),
/* 1*/	[1]	= RMIB_FUNC(CTLTYPE_STRING | RMIB_RO, sizeof(typec_str),
		    typec_func, "typec", "What is at the other end of the "
		    "USB-C cable: none, source, sink, audio, debug or unknown"),
/* 2*/	[2]	= RMIB_FUNC(CTLTYPE_STRING | RMIB_RO, sizeof(vbus_str),
		    vbus_func, "vbus", "Where VBUS comes from: none, external "
		    "or boost"),
};

static struct rmib_node pmic_node = RMIB_NODE(RMIB_RO, pmic_table, "pmic",
	"Qualcomm PMIC");
static int pmic_node_registered;

static ssize_t
nextboot_func(struct rmib_call *call, struct rmib_node *UNUSED(node),
	struct rmib_oldp *oldp, struct rmib_newp *newp)
{
	uint8_t spare;
	ssize_t r;
	int s;

	if (rd8(SID_PM660, PON_SOFT_RB_SPARE, &spare) != OK_SPMI)
		return EIO;
	mode_string(spare, nextboot_str, sizeof(nextboot_str));
	if ((r = rmib_readwrite(call, &nextboot_data, oldp, newp)) < 0 ||
	    newp == NULL)
		return r;
	if ((s = nextboot_set(nextboot_str)) != OK)
		return s;
	return r;
}

static void
nextboot_init(void)
{
	static const int mib[] = { CTL_MINIX, MINIX_PMIC };
	char arg[16];
	int r;

	pon_report();
	if (env_get_param("nextboot", arg, sizeof(arg)) == OK)
		(void)nextboot_set(arg);
	if ((r = rmib_register(mib, __arraycount(mib), &pmic_node)) != OK)
		log_warn(&pmic_log, "no sysctl minix.pmic: %d\n", r);
	else
		pmic_node_registered = 1;
}

/*===========================================================================*
 *    the driver                                                              *
 *===========================================================================*/
static void
arm(void)
{
	(void)sys_setalarm(sys_hz() / POLL_TICKS_HZ > 0 ?
	    sys_hz() / POLL_TICKS_HZ : 1, 0);
}

static void
pmic_alarm(clock_t UNUSED(stamp))
{
	polls++;
	buttons_poll();
	if (wd_armed && polls % WD_PET_EVERY == 0)
		(void)wr8(SID_PM660, CHG_WD_PET, 0x01);
	if (polls % BATT_EVERY == 0)
		battery_poll(0);
	if (polls % TYPEC_EVERY == 0)
		typec_poll();
	if (polls == LDO_REPORT_AGAIN)
		ldo_report();
	arm();
}

/* The MIB service, with a request for minix.pmic. */
static void
pmic_other(message *m, int ipc_status)
{
	if (m->m_source == MIB_PROC_NR)
		rmib_process(m, ipc_status);
}

static struct inputdriver pmic_tab = {
	.idr_alarm	= pmic_alarm,
	.idr_other	= pmic_other
};

/*
 * Which of the PM660's LDOs are on, read and only read: the one a device on
 * the board is powered from is somewhere in this line.  The touch screen of
 * the Mi Pad 4 did not answer on its bus; this line showed its named
 * supply (L11A) on, and the regulator it lacked was one only the RPM
 * switches (qcom_rpm).
 *
 * The regulators are on the PMIC's second SID (pmic@1 in the tree, its
 * "regulators" node); SID 0 at 0x4000 is the fuel gauge, which is where the
 * first version of this looked and found nothing.  The second looked for
 * type 0x21, which is SPMI_REGULATOR_TYPE_ULT_LDO; the PM660's LDOs are
 * SPMI_REGULATOR_TYPE_LDO, 0x04 (qcom_spmi-regulator.c), and LDO n is at
 * 0x4000 + (n - 1) * 0x100 (pm660_regulators[]; L4 is not accessible).
 * Enabled is ENABLE (0x46) bit 7, as spmi_regulator_common_is_enabled()
 * reads it - for an RPM-owned regulator, what the RPM made of every vote.
 * "*" marks one this execution environment owns and could switch itself.
 * The PM660L's LDOs the same way on its SID 3 (pmic@3, pm660l_regulators[]:
 * L1-L10 from 0x4000) - L3B among them, the 3.0 V the touch screen lacked.
 * Printed at start and again ten seconds later, after qcom_rpm.
 */
#define SID_PM660_REGS		1
#define SID_PM660L_REGS		3
#define LDO_FIRST		0x4000
#define PERIPH_TYPE		0x04
#define TYPE_LDO		0x04
#define TYPE_ULT_LDO		0x21
#define LDO_ENABLE		0x46

static void
ldo_report_one(const char *pmic, unsigned sid, unsigned top)
{
	char on[128], off[128];
	int lon = 0, loff = 0;
	unsigned base, n;
	uint8_t t, en;
	const char *mine;

	on[0] = off[0] = '\0';
	for (base = LDO_FIRST; base <= top; base += 0x100) {
		if (rd8(sid, base + PERIPH_TYPE, &t) != OK_SPMI ||
		    (t != TYPE_LDO && t != TYPE_ULT_LDO) ||
		    rd8(sid, base + LDO_ENABLE, &en) != OK_SPMI)
			continue;
		n = (base - LDO_FIRST) / 0x100 + 1;
		mine = spmi_writable(&spmi, sid, base) ? "*" : "";
		if (en & 0x80) {
			if (lon < (int)sizeof(on) - 8)
				lon += snprintf(on + lon, sizeof(on) - lon,
				    " %u%s", n, mine);
		} else if (loff < (int)sizeof(off) - 8)
			loff += snprintf(off + loff, sizeof(off) - loff,
			    " %u%s", n, mine);
	}
	log_info(&pmic_log, "%s LDOs on:%s; off:%s\n", pmic,
	    lon ? on : " none", loff ? off : " none");
}

static void
ldo_report(void)
{
	ldo_report_one("PM660", SID_PM660_REGS, 0x5200);	/* L19 */
	ldo_report_one("PM660L", SID_PM660L_REGS, 0x4900);	/* L10 */
}

static int
pmic_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	long v;
	int r;

	if (env_parse("log", "d", 0, &v, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		pmic_log.log_level = (int)v;
	if ((r = arb_init()) != OK)
		return r;
	buttons_init();
	nextboot_init();
	ldo_report();
	battery_init();
	battery_poll(1);
	typec_init();
	inputdriver_announce(INPUT_DEV_KBD);
	arm();
	return OK;
}

static void
pmic_signal(int signo)
{
	if (signo != SIGTERM)
		return;
	if (pmic_node_registered)
		(void)rmib_deregister(&pmic_node);
	inputdriver_terminate();
}

int
main(int argc, char **argv)
{
	env_setargs(argc, argv);
	sef_setcb_init_fresh(pmic_init);
	sef_setcb_signal_handler(pmic_signal);
	sef_startup();
	inputdriver_task(&pmic_tab);
	return 0;
}
