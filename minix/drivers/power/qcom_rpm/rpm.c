/*
 * qcom_rpm: the regulators a Qualcomm machine's tree says are always on,
 * asked of the RPM - what Linux's regulator core does at boot.
 *
 * On a Qualcomm SoC most of the PMIC's regulators belong to the RPM, a
 * separate processor; the application cores ask it for a regulator over
 * GLINK in the RPM's message RAM (glink.c), and the PMIC itself refuses
 * their writes.  The boot loader asks for what it needs and nothing more.
 * Linux then registers every regulator of the "qcom,rpm-*-regulators"
 * nodes, and set_machine_constraints() enables each one marked
 * "regulator-always-on" or "regulator-boot-on" - with "uv" when its
 * minimum and maximum are one value (apply_uV), "swen" = 1 always
 * (qcom_smd-regulator.c, rpm_reg_write_active()).  On the Mi Pad 4 the
 * touch controller did not answer on its bus until this ran.  Its tree
 * names only the 1.8 V vcc (L11A), and that one the boot loader had left
 * on - qcom_pmic read it so on the tablet; what was missing is one of the
 * others, by all appearances L3B, 3.0 V, the controller's analogue supply,
 * which the tree keeps on without naming a consumer.
 *
 * This driver does that much and no more: no voltage that the tree does
 * not pin, no regulator that it does not mark, no disabling, no loads.
 *
 * Then it stays, and takes requests from other services (QRPM_SET,
 * <minix/qcom_rpm.h>): one key for the active set, the level ("vlvl") or
 * the enable ("swen") of the CX and MX rails only - what rpmpd.c sends for
 * a power domain.  The adreno service votes VDDMX for the GPU's clock this
 * way (port/clover/hw-gpu.md, §20).  Who may send is system.conf's to say.
 * The tables of resource types are Linux's (rpm_pm660_regulators and
 * rpm_pm660l_regulators); a node of another PMIC is named in the log and
 * left alone.  When it is done the driver stays, idle, so that RS does not
 * start it again.  port/clover/hw-rpm.md.
 */

#include <minix/drivers.h>
#include <minix/fdt.h>
#include <minix/log.h>
#include <minix/qcom_rpm.h>
#include <minix/sysutil.h>
#include <minix/vm.h>

#include <sys/mman.h>

#include <stdlib.h>
#include <string.h>

#include "glink.h"

static struct log rlog = {
	.name = "rpm",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/* Linux's qcom_smd-regulator.c tables, by node name. */
struct rpm_reg {
	const char *name;
	uint32_t type, id;
};

static const struct rpm_reg pm660[] = {
	{ "s1", RPM_TYPE_SMPA, 1 }, { "s2", RPM_TYPE_SMPA, 2 },
	{ "s3", RPM_TYPE_SMPA, 3 }, { "s4", RPM_TYPE_SMPA, 4 },
	{ "s5", RPM_TYPE_SMPA, 5 }, { "s6", RPM_TYPE_SMPA, 6 },
	{ "l1", RPM_TYPE_LDOA, 1 }, { "l2", RPM_TYPE_LDOA, 2 },
	{ "l3", RPM_TYPE_LDOA, 3 },
	/* l4 is unaccessible on PM660 */
	{ "l5", RPM_TYPE_LDOA, 5 }, { "l6", RPM_TYPE_LDOA, 6 },
	{ "l7", RPM_TYPE_LDOA, 7 }, { "l8", RPM_TYPE_LDOA, 8 },
	{ "l9", RPM_TYPE_LDOA, 9 }, { "l10", RPM_TYPE_LDOA, 10 },
	{ "l11", RPM_TYPE_LDOA, 11 }, { "l12", RPM_TYPE_LDOA, 12 },
	{ "l13", RPM_TYPE_LDOA, 13 }, { "l14", RPM_TYPE_LDOA, 14 },
	{ "l15", RPM_TYPE_LDOA, 15 }, { "l16", RPM_TYPE_LDOA, 16 },
	{ "l17", RPM_TYPE_LDOA, 17 }, { "l18", RPM_TYPE_LDOA, 18 },
	{ "l19", RPM_TYPE_LDOA, 19 },
	{ NULL, 0, 0 }
};

static const struct rpm_reg pm660l[] = {
	{ "s1", RPM_TYPE_SMPB, 1 }, { "s2", RPM_TYPE_SMPB, 2 },
	{ "s3", RPM_TYPE_RWCX, 0 }, { "s5", RPM_TYPE_RWMX, 0 },
	{ "l1", RPM_TYPE_LDOB, 1 }, { "l2", RPM_TYPE_LDOB, 2 },
	{ "l3", RPM_TYPE_LDOB, 3 }, { "l4", RPM_TYPE_LDOB, 4 },
	{ "l5", RPM_TYPE_LDOB, 5 }, { "l6", RPM_TYPE_LDOB, 6 },
	{ "l7", RPM_TYPE_LDOB, 7 }, { "l8", RPM_TYPE_LDOB, 8 },
	{ "l9", RPM_TYPE_RWLC, 0 }, { "l10", RPM_TYPE_RWLM, 0 },
	{ "bob", RPM_TYPE_BOBB, 1 },
	{ NULL, 0, 0 }
};

static const struct {
	const char *compatible;
	const struct rpm_reg *table;
	const char *suffix;		/* Linux's regulator names: l11a */
} pmics[] = {
	{ "qcom,rpm-pm660-regulators",	pm660,	"a" },
	{ "qcom,rpm-pm660l-regulators",	pm660l,	"b" },
};

/* qcom-apcs-ipc-mailbox.c: where the IPC register is in the block. */
static const struct {
	const char *compatible;
	unsigned offset;
} apcs[] = {
	{ "qcom,sdm660-apcs-hmss-global",	8 },
	{ "qcom,msm8994-apcs-kpss-global",	8 },
	{ "qcom,msm8916-apcs-kpss-global",	8 },
	{ "qcom,msm8996-apcs-hmss-global",	16 },
};

#define MAX_REGS	48

/* What the walk found. */
static struct {
	int found;
	uint32_t msg_ram_ph, mbox_ph, mbox_bit;
	u64_t mbox_pa, mbox_sz;
	unsigned mbox_off;
	int mbox_known;
	unsigned nregs;
	struct {
		char name[8];
		uint32_t type, id, uv;
		int boot_on;
	} regs[MAX_REGS];
} dt;

static int pmic_depth = -1;
static const struct rpm_reg *pmic_table;
static const char *pmic_suffix;

static volatile uint8_t *ipc;
static uint32_t ipc_bit;
static struct glink g;		/* the channel, once rpm_init() opened it */
static int g_open;

static uint32_t
prop_u32(const struct fdt_node *n, const char *p, unsigned i, uint32_t dflt)
{
	const void *v;
	unsigned len;

	if ((v = fdt_getprop(n, p, &len)) == NULL || len < 4 * (i + 1))
		return dflt;
	return (uint32_t)fdt_read_cells((const char *)v + 4 * i, 1);
}

static int
find_cb(void *cookie, int depth, const char *name, const struct fdt_node *n)
{
	unsigned i, len;
	const struct rpm_reg *r;
	uint32_t min, max;

	(void)cookie;
	if (pmic_depth >= 0 && depth <= pmic_depth)
		pmic_depth = -1;

	if (fdt_node_is_compatible(n, "qcom,glink-rpm") && !dt.found) {
		dt.found = 1;
		dt.msg_ram_ph = prop_u32(n, "qcom,rpm-msg-ram", 0, 0);
		dt.mbox_ph = prop_u32(n, "mboxes", 0, 0);
		dt.mbox_bit = prop_u32(n, "mboxes", 1, 0);
		return 0;
	}

	for (i = 0; i < sizeof(pmics) / sizeof(pmics[0]); i++)
		if (fdt_node_is_compatible(n, pmics[i].compatible)) {
			pmic_depth = depth;
			pmic_table = pmics[i].table;
			pmic_suffix = pmics[i].suffix;
			return 0;
		}

	if (pmic_depth >= 0 && depth == pmic_depth + 1 &&
	    (fdt_getprop(n, "regulator-always-on", &len) != NULL ||
	    fdt_getprop(n, "regulator-boot-on", &len) != NULL)) {
		for (r = pmic_table; r->name != NULL; r++)
			if (strcmp(r->name, name) == 0)
				break;
		if (r->name == NULL) {
			log_warn(&rlog, "%s%s: not in this driver's tables; "
			    "left alone\n", name, pmic_suffix);
			return 0;
		}
		if (dt.nregs == MAX_REGS)
			return 0;
		snprintf(dt.regs[dt.nregs].name, sizeof(dt.regs[0].name),
		    "%s%s", name, pmic_suffix);
		dt.regs[dt.nregs].type = r->type;
		dt.regs[dt.nregs].id = r->id;
		min = prop_u32(n, "regulator-min-microvolt", 0, 0);
		max = prop_u32(n, "regulator-max-microvolt", 0, 0);
		dt.regs[dt.nregs].uv = (min != 0 && min == max) ? min : 0;
		dt.nregs++;
	}
	return 0;
}

/* The mailbox: its node by phandle, for the compatible string. */
static uint32_t want_ph;

static int
mbox_cb(void *cookie, int depth, const char *name, const struct fdt_node *n)
{
	unsigned i;

	(void)cookie; (void)depth; (void)name;
	if (prop_u32(n, "phandle", 0, 0) != want_ph)
		return 0;
	for (i = 0; i < sizeof(apcs) / sizeof(apcs[0]); i++)
		if (fdt_node_is_compatible(n, apcs[i].compatible)) {
			dt.mbox_off = apcs[i].offset;
			dt.mbox_known = 1;
			break;
		}
	if (fdt_node_reg(n, 0, &dt.mbox_pa, &dt.mbox_sz) != 0)
		dt.mbox_sz = 0;
	return 1;
}

static void
kick(void *ctx)
{
	(void)ctx;
	*(volatile uint32_t *)(ipc + dt.mbox_off) = ipc_bit;
}

static void
delay(void *ctx, unsigned us)
{
	(void)ctx;
	micro_delay(us);
}

static int
rpm_init(int UNUSED(type), sef_init_info_t *UNUSED(info))
{
	static const uint32_t keys[2] = { RPM_KEY_SWEN, RPM_KEY_UV };
	uint32_t values[2];
	u64_t pa, sz;
	void *dtb, *v;
	unsigned i, ok = 0;
	long lv;
	int r;

	if (env_parse("log", "d", 0, &lv, LEVEL_NONE, LEVEL_TRACE) == EP_SET)
		rlog.log_level = (int)lv;
	if ((dtb = fdt_fetch()) == NULL)
		return ENXIO;
	(void)fdt_walk(dtb, find_cb, NULL);
	if (!dt.found || dt.msg_ram_ph == 0 || dt.mbox_ph == 0) {
		log_info(&rlog, "no GLINK RPM edge in the tree; nothing to "
		    "do\n");
		free(dtb);
		return ENXIO;
	}
	want_ph = dt.mbox_ph;
	(void)fdt_walk(dtb, mbox_cb, NULL);
	r = fdt_phandle_reg(dtb, dt.msg_ram_ph, 0, &pa, &sz);
	free(dtb);
	if (r != 0 || sz == 0 || !dt.mbox_known || dt.mbox_sz == 0 ||
	    dt.mbox_off + 4 > dt.mbox_sz || dt.mbox_bit > 31) {
		log_warn(&rlog, "message RAM or mailbox not usable (ram %d, "
		    "mailbox %s at 0x%llx)\n", r, dt.mbox_known ? "known" :
		    "of an unknown kind", (unsigned long long)dt.mbox_pa);
		return ENXIO;
	}
	ipc_bit = 1u << dt.mbox_bit;
	log_info(&rlog, "message RAM 0x%llx+0x%llx, IPC 0x%llx bit %u; "
	    "%u regulators the tree keeps on\n", (unsigned long long)pa,
	    (unsigned long long)sz, (unsigned long long)dt.mbox_pa +
	    dt.mbox_off, dt.mbox_bit, dt.nregs);
	if (dt.nregs == 0)
		return ENXIO;

	if ((v = vm_map_phys(SELF, (void *)(phys_bytes)pa, (size_t)sz)) ==
	    MAP_FAILED ||
	    (ipc = vm_map_phys(SELF, (void *)(phys_bytes)dt.mbox_pa,
	    (size_t)dt.mbox_sz)) == MAP_FAILED) {
		log_warn(&rlog, "cannot map the message RAM or the mailbox; "
		    "not granted by RS?\n");
		return EPERM;
	}

	memset(&g, 0, sizeof(g));
	g.ram = v;
	g.ram_size = (size_t)sz;
	g.kick = kick;
	g.delay_us = delay;
	if ((r = glink_init(&g)) != GLINK_OK) {
		log_warn(&rlog, "no GLINK table of contents in the message RAM "
		    "(%d)\n", r);
		return r;
	}
	log_info(&rlog, "FIFOs: tx 0x%x+%u, rx 0x%x+%u; indices as found: tx "
	    "%u/%u, rx %u/%u (tail/head)\n", g.tx_fifo, g.tx_len, g.rx_fifo,
	    g.rx_len, g.found[0], g.found[1], g.found[2], g.found[3]);
	if ((r = glink_open(&g, "rpm_requests", 5000)) != GLINK_OK) {
		log_warn(&rlog, "rpm_requests did not open: %d (version %d, "
		    "open %d/%d, rcid %u)\n", r, g.version_ok, g.open_sent,
		    g.open_acked, g.rcid);
		return r;
	}
	log_info(&rlog, "rpm_requests open (lcid %u, rcid %u)\n", g.lcid,
	    g.rcid);
	g_open = 1;

	for (i = 0; i < dt.nregs; i++) {
		values[0] = 1;
		values[1] = dt.regs[i].uv;
		r = glink_rpm_request(&g, dt.regs[i].type, dt.regs[i].id, keys,
		    values, dt.regs[i].uv ? 2 : 1, 5000);
		if (r == GLINK_OK) {
			ok++;
			if (dt.regs[i].uv)
				log_info(&rlog, "%s on at %u uV\n",
				    dt.regs[i].name, dt.regs[i].uv);
			else
				log_info(&rlog, "%s on\n", dt.regs[i].name);
		} else
			log_warn(&rlog, "%s: %d%s%s\n", dt.regs[i].name, r,
			    g.rpm_err[0] ? ", RPM says: " : "", g.rpm_err);
	}
	log_info(&rlog, "%u of %u regulators asked on\n", ok, dt.nregs);
	return OK;
}

/*
 * Every service's own vote, and what goes to the RPM is the strongest of
 * them: the highest level, enabled if anyone wants it - what Linux's genpd
 * and rpmpd do between the GPU and the modem's loader, which both vote
 * VDDMX (the loader for as long as the modem boots, then lets go).  One
 * vote per service, rail and key; a vote of a service that went away
 * counts until its endpoint votes again.
 */
#define NVOTES		16

static struct vote {
	endpoint_t who;
	uint32_t type, id, key, value;
	int used;
} votes[NVOTES];

static uint32_t
aggregate(endpoint_t who, uint32_t type, uint32_t id, uint32_t key,
	uint32_t value, int *full)
{
	struct vote *v = NULL, *free_v = NULL;
	uint32_t agg = 0;
	unsigned i;

	*full = 0;
	for (i = 0; i < NVOTES; i++) {
		if (!votes[i].used) {
			if (free_v == NULL)
				free_v = &votes[i];
		} else if (votes[i].who == who && votes[i].type == type &&
		    votes[i].id == id && votes[i].key == key)
			v = &votes[i];
	}
	if (v == NULL) {
		if ((v = free_v) == NULL) {
			*full = 1;
			return 0;
		}
		v->used = 1;
		v->who = who;
		v->type = type;
		v->id = id;
		v->key = key;
	}
	v->value = value;
	for (i = 0; i < NVOTES; i++)
		if (votes[i].used && votes[i].type == type &&
		    votes[i].id == id && votes[i].key == key &&
		    votes[i].value > agg)
			agg = votes[i].value;
	return agg;
}

static const char *
res_name(uint32_t type)
{
	return type == QRPM_RES_RWMX ? "rwmx" : type == QRPM_RES_RWCX ?
	    "rwcx" : type == QRPM_RES_LDOA ? "ldoa" : type == QRPM_RES_CLKA ?
	    "clka" : "ipa clock";
}

static const char *
key_name(uint32_t key)
{
	return key == QRPM_KEY_LEVEL ? "level" : key == QRPM_KEY_ENABLE ?
	    "enable" : key == QRPM_KEY_UV ? "uV" : key == QRPM_KEY_PIN_ENABLE ?
	    "pin enable" : "kHz";
}

/* QRPM_SET: one key of a rail or the IPA's clock, for the active set. */
static int
do_set(const message *m)
{
	uint32_t type = m->m_u32.data[QRPM_TYPE], id = m->m_u32.data[QRPM_ID];
	uint32_t key = m->m_u32.data[QRPM_KEY];
	uint32_t value = m->m_u32.data[QRPM_VALUE];
	int r, full;

	if (!g_open)
		return ENODEV;
	if (type == QRPM_RES_LDOA && id == 5) {
		/*
		 * PM660's L5, the WCN3990's vdd-0.8-cx-mx: on/off, and the
		 * voltage inside the tree's range (525-952 mV; Linux kept
		 * it at 525 mV under Wi-Fi).
		 */
		if ((key != QRPM_KEY_ENABLE || value > 1) &&
		    (key != QRPM_KEY_UV || value < 525000 || value > 952000))
			return EPERM;
	} else if (type == QRPM_RES_CLKA && id == 4) {
		/* rf_clk1, the WCN3990's reference clock, by its pin. */
		if (key != QRPM_KEY_PIN_ENABLE || value > 1)
			return EPERM;
	} else if (id != 0)
		return EPERM;
	else if (type == QRPM_RES_IPA_CLK) {
		/* The IPA's clock, by its rate (the IPA asks 40 MHz). */
		if (key != QRPM_KEY_KHZ || value > 200000)
			return EPERM;
	} else if ((type != QRPM_RES_RWCX && type != QRPM_RES_RWMX) ||
	    (key != QRPM_KEY_LEVEL && key != QRPM_KEY_ENABLE) ||
	    (key == QRPM_KEY_ENABLE && value > 1))
		return EPERM;
	value = aggregate(m->m_source, type, id, key, value, &full);
	if (full)
		return ENOSPC;
	r = glink_rpm_request(&g, type, id, &key, &value, 1, 1000);
	if (r == GLINK_OK)
		log_info(&rlog, "%s %s %u for %d\n", res_name(type),
		    key_name(key), value, m->m_source);
	else
		log_warn(&rlog, "%s %s %u for %d: %d%s%s\n", res_name(type),
		    key_name(key), value, m->m_source, r,
		    g.rpm_err[0] ? ", RPM says: " : "", g.rpm_err);
	return r;
}

int
main(int argc, char **argv)
{
	message m, reply;
	int ipc_status;

	env_setargs(argc, argv);
	sef_setcb_init_fresh(rpm_init);
	sef_startup();
	for (;;) {
		if (sef_receive_status(ANY, &m, &ipc_status) != OK)
			continue;
		if (is_ipc_notify(ipc_status) || m.m_type != QRPM_SET)
			continue;
		memset(&reply, 0, sizeof(reply));
		reply.m_type = do_set(&m);
		(void)ipc_send(m.m_source, &reply);
	}
	return 0;
}
