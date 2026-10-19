/*
 * The stages (port/clover/hw-cpu.md, section 10).
 *
 *   probe  the fuses and this chip's corners, aux, the rails, each
 *          cluster's mux and PLL, and the highest point path A allows:
 *          read, printed, nothing written
 *   set    every cluster given a target goes there: gold first (its cores
 *          already run from aux, so its PLL is free), then silver.  With
 *          steps on, through every point of the table between where the
 *          cluster is and the target, upwards; each point is the whole
 *          aux -> PLL -> mux round of cf_go(), and cf->hold seconds are
 *          spent at it.  The first refusal or failure stops the run: what
 *          is set stays set, and the cluster that failed is left on aux
 *          or where it was.
 *   gov    stay, and move each cluster between its points by load
 *          (cf_gov.c): the stage here starts it - every cluster at the
 *          point its policy starts from - and cpufreq.c runs the periods.
 *          gold= and silver= are caps here: a point, "max" (none) or
 *          "aux" (only aux).
 *
 * A target is a point of the cluster's table for this chip's speed bin
 * that a PLL makes ("1747", "1747.2", "1536"...), "max" - the highest
 * point the rail holds now - or "aux", GPLL0 (the undo).  Every target is
 * parsed before anything is written.
 */

#include "qcom_cpufreq.h"

#define MHZ(k)		(unsigned)((k) / 1000), (unsigned)((k) % 1000 / 100)

/* A target, against the cluster's table (only after cf_volt()). */
int
cf_parse_target(const struct cf_corners *c, const char *s, uint32_t *khz)
{
	unsigned long mhz = 0;
	unsigned frac = 0, i, digits = 0;
	int has_frac = 0;
	const char *p;

	if (strcmp(s, "aux") == 0) {
		*khz = CF_TARGET_AUX;
		return OK;
	}
	if (strcmp(s, "max") == 0) {
		*khz = CF_TARGET_MAX;
		return OK;
	}
	for (p = s; *p >= '0' && *p <= '9' && digits < 5; p++, digits++)
		mhz = mhz * 10 + (unsigned long)(*p - '0');
	if (digits == 0)
		return EINVAL;
	if (*p == '.' && p[1] >= '0' && p[1] <= '9') {
		frac = (unsigned)(p[1] - '0');
		has_frac = 1;
		p += 2;
	}
	if (*p != '\0')
		return EINVAL;
	for (i = 0; i < c->n; i++)
		if (c->khz[i] / 1000 == mhz && cf_point_l(c->khz[i]) != 0 &&
		    (!has_frac || c->khz[i] % 1000 / 100 == frac)) {
			*khz = c->khz[i];
			return OK;
		}
	return EINVAL;
}

/* One cluster as it is: its rate, its PLL, its rails. */
static void
report(struct cf *cf, const struct cf_cluster *cl)
{
	struct cf_state st;
	unsigned i, mv;
	uint32_t top;

	if (cf_state(cf, cl, &st) == OK)
		log_info(&cf_log, "%s: %u.%u MHz, %s; mux 0x%x, PLL mode 0x%x "
		    "L 0x%x (%u.%u MHz) %s\n", cl->name, MHZ(st.khz),
		    st.on_pll ? "from its PLL" : "from aux", st.cfg, st.mode,
		    st.l, MHZ(st.l * CF_XO_KHZ),
		    (st.status & (1u << 16)) ? "locked" : "not locked");
	else
		log_info(&cf_log, "%s: %s (mux cmd 0x%x cfg 0x%x, PLL mode 0x%x "
		    "L 0x%x status 0x%x)\n", cl->name, st.why, st.cmd, st.cfg,
		    st.mode, st.l, st.status);
	for (i = 0; i < cl->nrail; i++)
		if (cf_rail_mv(&cf->spmi, &cl->rail[i], &mv) == OK)
			log_info(&cf_log, "%s: %s set point %u mV\n", cl->name,
			    cl->rail[i].name, mv);
	if (cf_highest(cf, cl, &top) == OK)
		log_info(&cf_log, "%s: the rail holds up to %u.%u MHz (open "
		    "loop %d mV)\n", cl->name, MHZ(top),
		    (int)(cl->c.ol_uv[cf_corner_of(&cl->c, top)] / 1000));
	else
		log_info(&cf_log, "%s: the rail holds no point of the table\n",
		    cl->name);
}

static void
table(const struct cf_cluster *cl)
{
	const struct cf_corners *c = &cl->c;
	unsigned i;

	for (i = 0; i < CF_FUSE_CORNERS; i++)
		log_info(&cf_log, "%s fuse corner %-8s raw 0x%02x %d mV\n",
		    cl->name, c->fc_name[i], c->raw[i],
		    (int)(c->fused_uv[i] / 1000));
	for (i = 0; i < c->n; i++)
		log_info(&cf_log, "%s corner %u: %u.%u MHz, open loop %d mV, "
		    "floor %d mV%s\n", cl->name, i + 1, MHZ(c->khz[i]),
		    (int)(c->ol_uv[i] / 1000), (int)(c->floor_uv[i] / 1000),
		    c->ol_uv[i] >= CF_APM_UV ? " (APM)" : "");
}

/* One cluster to its target, by steps when asked. */
static int
set(struct cf *cf, struct cf_cluster *cl, uint32_t khz)
{
	struct cf_state st;
	uint32_t ladder[CF_MAX_CORNERS + 1], from;
	unsigned i, n = 0;
	int r;

	if (cf_state(cf, cl, &st) != OK) {
		log_warn(&cf_log, "%s: %s; not touching it\n", cl->name,
		    st.why);
		return EINVAL;
	}
	from = st.khz;
	/* A target the rail does not hold is refused before the first step. */
	if (khz != 0 && khz != from && (r = cf_held(cf, cl, khz, 0)) != OK)
		return r;
	if (khz != 0 && cf->steps && khz > from)
		for (i = 0; i < cl->c.n; i++)
			if (cl->c.khz[i] > from && cl->c.khz[i] < khz &&
			    cf_point_l(cl->c.khz[i]) != 0)
				ladder[n++] = cl->c.khz[i];
	ladder[n++] = khz;
	for (i = 0; i < n; i++) {
		if (ladder[i] == 0)
			log_info(&cf_log, "%s: to aux\n", cl->name);
		else
			log_info(&cf_log, "%s: to %u.%u MHz (L 0x%x)\n",
			    cl->name, MHZ(ladder[i]), cf_point_l(ladder[i]));
		if ((r = cf_go(cf, cl, ladder[i])) != OK) {
			report(cf, cl);
			return r;
		}
		report(cf, cl);
		if (cf->hold > 0 && i + 1 < n)
			cf_pause(cf->hold);
	}
	return OK;
}

/* stage=gov: the caps understood, then every cluster to its start. */
static int
gov(struct cf *cf, const char *const target[CF_NCLUSTER])
{
	static const int order[CF_NCLUSTER] = { CF_GOLD, CF_SILVER };
	static const char *const pname[] = { "performance", "powersave",
	    "ondemand" };
	struct cf_cluster *cl;
	uint32_t khz;
	unsigned k;
	int id, r, started = 0;

	for (id = 0; id < CF_NCLUSTER; id++) {
		cl = &cf->cl[id];
		memset(&cf->gov[id], 0, sizeof(cf->gov[id]));
		if (target[id] == NULL)
			continue;
		if (cf_parse_target(&cl->c, target[id], &khz) != OK) {
			log_warn(&cf_log, "%s=%s: not aux, max or a point of "
			    "this chip's table a PLL makes\n", cl->name,
			    target[id]);
			return EINVAL;
		}
		cf->gov[id].cap = khz == CF_TARGET_MAX ? 0 :
		    khz == CF_TARGET_AUX ? 1 : khz;
	}
	log_info(&cf_log, "governor: %s, up %u%%, aim %u%%, down after %u\n",
	    pname[cf->pol.policy], cf->pol.up, cf->pol.aim, cf->pol.downs);
	for (k = 0; k < CF_NCLUSTER; k++) {
		id = order[k];
		cl = &cf->cl[id];
		if ((r = cf_gov_start(cf, cl, &cf->gov[id], &cf->pol)) != OK)
			log_warn(&cf_log, "%s: not governed (%d)\n", cl->name, r);
		else
			started++;
		report(cf, cl);
	}
	return started > 0 ? OK : EPERM;
}

int
cf_run(struct cf *cf, int stage, const char *const target[CF_NCLUSTER])
{
	static const int order[CF_NCLUSTER] = { CF_GOLD, CF_SILVER };
	uint64_t rows[CF_FUSE_ROWS];
	uint32_t khz[CF_NCLUSTER];
	struct cf_cluster *cl;
	unsigned k, given = 0;
	int id, r;

	if ((r = cf_fuse_rows(cf, rows)) != OK)
		return r;
	for (id = 0; id < CF_NCLUSTER; id++) {
		cl = &cf->cl[id];
		if ((r = cf_volt(id, rows, &cl->c)) != OK) {
			log_warn(&cf_log, "speed bin %u: the vendor tree has "
			    "no %s corners for it; refusing\n",
			    (unsigned)(rows[38] >> 29) & 7, cl->name);
			return r;
		}
	}
	log_info(&cf_log, "speed bin %u, CPR fusing revision %u\n",
	    cf->cl[0].c.speed_bin, cf->cl[0].c.cpr_rev);
	for (id = 0; id < CF_NCLUSTER; id++)
		table(&cf->cl[id]);
	if (cf_aux(cf) == OK)
		log_info(&cf_log, "aux (GPLL0): %u.%u MHz\n",
		    MHZ(cf->aux_khz));
	for (id = 0; id < CF_NCLUSTER; id++)
		report(cf, &cf->cl[id]);
	if (stage == CF_PROBE)
		return OK;
	if (stage == CF_GOV)
		return gov(cf, target);

	/* Every target understood before anything is written. */
	for (id = 0; id < CF_NCLUSTER; id++) {
		khz[id] = 0;
		if (target[id] == NULL)
			continue;
		cl = &cf->cl[id];
		if (cf_parse_target(&cl->c, target[id], &khz[id]) != OK) {
			log_warn(&cf_log, "%s=%s: not aux, max or a point of "
			    "this chip's table a PLL makes\n", cl->name,
			    target[id]);
			return EINVAL;
		}
		if (khz[id] == CF_TARGET_MAX &&
		    cf_highest(cf, cl, &khz[id]) != OK) {
			log_warn(&cf_log, "%s=max: the rail holds no point\n",
			    cl->name);
			return EPERM;
		}
		if (khz[id] == CF_TARGET_AUX)
			khz[id] = 0;
		given++;
	}
	if (given == 0) {
		log_warn(&cf_log, "stage=set without gold= or silver=: "
		    "nothing to set\n");
		return EINVAL;
	}
	for (k = 0; k < CF_NCLUSTER; k++) {
		id = order[k];
		if (target[id] == NULL)
			continue;
		if ((r = set(cf, &cf->cl[id], khz[id])) != OK) {
			log_warn(&cf_log, "%s: stopped (%d)\n",
			    cf->cl[id].name, r);
			return r;
		}
	}
	return OK;
}
