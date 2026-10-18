/*
 * The CPU rails' set points, read - and only read - through the SPMI
 * arbiter's observer channels (port/clover/hw-cpu.md, sections 6 and 8).
 *
 * VDD_APC0 (silver) is PM660 S1, VDD_APC1 (gold) is S2 and S3, two phases
 * of one rail; all three are FTS426 bucks on SID 1, and their channels
 * belong to EE 0, the processor, so it is the boot loader's set point that
 * holds until somebody writes another.  This service never does: it has no
 * write to the PMIC anywhere (spmi_write() is linked, never called).  The
 * observer channels of these three regulators are used by nobody else -
 * qcom_pmic reads and writes other peripherals.
 *
 * How S2 and S3 share one set point (leader and follower, or each its own)
 * is not known yet (hw-cpu.md, section 6); the gold cluster's set point is
 * taken as the lower of the two, which is the safe answer either way.
 */

#include "qcom_cpufreq.h"

#define REG_TYPE	0x04	/* type, subtype */
#define REG_VSET	0x40	/* FTS426: set point in mV, 16 bits, LE */
#define REG_EN_CTL	0x46
#define EN_CTL_ENABLE	0x80

#define FTS426_TYPE	0x1c
#define FTS426_SUBTYPE	0x0a
#define FTS426_MIN_MV	320	/* the regulator's range (hw-cpu.md, section 6) */
#define FTS426_MAX_MV	1352

/*
 * One regulator's set point: it must be an FTS426 (the only layout this
 * file knows), enabled, inside the regulator's range, and read the same
 * twice.  Anything else is an error, not a guess.
 */
int
cf_rail_mv(struct spmi *s, const struct cf_rail *r, unsigned *mv)
{
	uint8_t t[2], v[2], w[2], en;
	unsigned a, b;

	if (spmi_read(s, r->sid, r->base + REG_TYPE, t, 2) != OK_SPMI ||
	    spmi_read(s, r->sid, r->base + REG_EN_CTL, &en, 1) != OK_SPMI ||
	    spmi_read(s, r->sid, r->base + REG_VSET, v, 2) != OK_SPMI ||
	    spmi_read(s, r->sid, r->base + REG_VSET, w, 2) != OK_SPMI) {
		log_warn(&cf_log, "%s: the PMIC did not answer\n", r->name);
		return EIO;
	}
	if (t[0] != FTS426_TYPE || t[1] != FTS426_SUBTYPE) {
		log_warn(&cf_log, "%s: type 0x%02x subtype 0x%02x, not an "
		    "FTS426\n", r->name, t[0], t[1]);
		return ENODEV;
	}
	if (!(en & EN_CTL_ENABLE)) {
		log_warn(&cf_log, "%s: not enabled (0x%02x)\n", r->name, en);
		return EPERM;
	}
	a = (unsigned)v[1] << 8 | v[0];
	b = (unsigned)w[1] << 8 | w[0];
	if (a != b || a < FTS426_MIN_MV || a > FTS426_MAX_MV) {
		log_warn(&cf_log, "%s: set point %u mV, then %u mV: not one "
		    "this service believes\n", r->name, a, b);
		return ERANGE;
	}
	*mv = a;
	return OK;
}

/* A cluster's set point: the lowest of its regulators'. */
int
cf_rails_mv(struct cf *cf, const struct cf_cluster *cl, unsigned *mv)
{
	unsigned i, v, lo = 0;
	int r;

	if (cl->nrail == 0)
		return ENODEV;
	for (i = 0; i < cl->nrail; i++) {
		if ((r = cf_rail_mv(&cf->spmi, &cl->rail[i], &v)) != OK)
			return r;
		log_debug(&cf_log, "%s: %s %u mV\n", cl->name,
		    cl->rail[i].name, v);
		if (i == 0 || v < lo)
			lo = v;
	}
	*mv = lo;
	return OK;
}
