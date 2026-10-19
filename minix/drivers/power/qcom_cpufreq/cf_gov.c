/*
 * The governor: stage=gov keeps the service alive and moves each cluster
 * between the points it may run at, by load (port/clover/hw-cpu.md, path A
 * of section 9, and the governor's section).
 *
 * The points of a cluster are aux - GPLL0, 600 MHz, on which the PLL can
 * be switched off - and the points of the cluster's table for this chip
 * that a PLL makes and that the rails hold now, up to a cap (gold=,
 * silver=; "max" by default).  Every change is cf_go()'s round, with the
 * rails read anew before it: a point the rails stopped holding is refused
 * there, and then the list is made again from what they hold now.  The
 * voltage is never changed - only the frequency at the voltage the boot
 * loader set - so what a lower point saves is the clock's own power: the
 * cores' and the L2's switching while they run, the PLL when it is off.
 *
 * The policies:
 *   performance  the top point, always (what stage=set ... max did)
 *   powersave    aux, always, the PLL off
 *   ondemand     a load at or above up% goes straight to the top; below
 *                it, the lowest point at which the load would be at most
 *                aim%, upwards at once, downwards only after downs
 *                periods in a row have asked for lower.
 *
 * The load of a cluster is its busiest core's, over the last period: the
 * share of the period it spent not idle, from the kernel's per-core
 * account (cpufreq.c).  Which core is in which cluster comes from the
 * device tree, numbered as the kernel numbers them (cf_gov_cpus()).
 */

#include "qcom_cpufreq.h"

#include <minix/fdt.h>

#define MHZ(k)		(unsigned)((k) / 1000), (unsigned)((k) % 1000 / 100)
#define MPIDR_AFF_MASK	0xff00ffffffull
#define FAILS_TO_FREEZE	5

/*===========================================================================*
 *    the cores                                                              *
 *===========================================================================*/
struct cpu_scan {
	int in_cpus;
	unsigned addr_cells, count;
	uint64_t mpidr[CF_MAX_CPUS];
};

/* Every node of /cpus with device_type "cpu", in the tree's order. */
static int
scan_cpu(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct cpu_scan *s = cookie;
	const void *p;
	unsigned len;

	if (depth == 1) {
		s->in_cpus = strcmp(name, "cpus") == 0;
		if (s->in_cpus) {
			s->addr_cells = 1;
			if ((p = fdt_getprop(node, "#address-cells", &len)) !=
			    NULL && len == 4)
				s->addr_cells = (unsigned)fdt_read_cells(p, 1);
		}
		return 0;
	}
	if (depth != 2 || !s->in_cpus)
		return 0;
	if ((p = fdt_getprop(node, "device_type", &len)) == NULL ||
	    strcmp((const char *)p, "cpu") != 0)
		return 0;
	if ((p = fdt_getprop(node, "reg", &len)) == NULL ||
	    len < 4 * s->addr_cells)
		return 0;
	if (s->count >= CF_MAX_CPUS)
		return 1;
	s->mpidr[s->count++] = fdt_read_cells(p, s->addr_cells) &
	    MPIDR_AFF_MASK;
	return 0;
}

/*
 * Which cluster each core number is in.  The kernel numbers the cores in
 * the tree's order, but with the boot core swapped into place zero
 * (discover_cpus() in arch_smp.c); the service cannot read the boot core's
 * MPIDR, so it is told (bootmpidr=, 0 by default: on this SoC the boot
 * loader starts the kernel on silver core 0x0, and the vendor kernel's
 * numbering under LineageOS - cores 0-3 silver - says the same).  Aff1 is
 * the cluster: 0 silver, 1 gold.
 */
int
cf_gov_cpus(const void *dtb, uint64_t boot_mpidr, int cluster_of[CF_MAX_CPUS],
	unsigned *ncpus)
{
	struct cpu_scan s;
	uint64_t t;
	unsigned i, aff1;

	memset(&s, 0, sizeof(s));
	(void)fdt_walk(dtb, scan_cpu, &s);
	if (s.count == 0)
		return ENOENT;
	for (i = 0; i < s.count && s.mpidr[i] != boot_mpidr; i++)
		;
	if (i == s.count)
		return ENOENT;
	t = s.mpidr[i];
	s.mpidr[i] = s.mpidr[0];
	s.mpidr[0] = t;
	for (i = 0; i < CF_MAX_CPUS; i++)
		cluster_of[i] = CF_NO_CLUSTER;
	for (i = 0; i < s.count; i++) {
		aff1 = (unsigned)(s.mpidr[i] >> 8) & 0xff;
		if ((s.mpidr[i] >> 16) == 0 && aff1 == 0)
			cluster_of[i] = CF_SILVER;
		else if ((s.mpidr[i] >> 16) == 0 && aff1 == 1)
			cluster_of[i] = CF_GOLD;
	}
	*ncpus = s.count;
	return OK;
}

/*===========================================================================*
 *    the points                                                             *
 *===========================================================================*/
/*
 * aux, then every point of the table a PLL makes above it, held by the
 * rails now and no higher than the cap.  The point the cluster is at is
 * found among them; one that is not (the boot loader's 1382.4 MHz on
 * silver, a point the rails stopped holding) counts as the nearest below,
 * marked off_point, so that the next decision moves it even to that one.
 */
int
cf_gov_points(struct cf *cf, const struct cf_cluster *cl, struct cf_govcl *g)
{
	struct cf_state st;
	unsigned i, mv;
	int r;

	/* One whole read of the rails for every point; none: as they were. */
	if ((r = cf_rails_mv(cf, cl, &mv)) != OK)
		return r;
	if (cf->aux_khz == 0 || cf_held_mv(cl, cf->aux_khz, mv) != OK) {
		log_warn(&cf_log, "%s: aux is not running or not held; no "
		    "points\n", cl->name);
		return EPERM;
	}
	g->n = 0;
	g->khz[g->n++] = cf->aux_khz;
	for (i = 0; i < cl->c.n && g->n < CF_GOV_MAXPTS; i++) {
		if (cl->c.khz[i] <= cf->aux_khz || cf_point_l(cl->c.khz[i]) == 0)
			continue;
		if (g->cap != 0 && cl->c.khz[i] > g->cap)
			continue;
		if (cf_held_mv(cl, cl->c.khz[i], mv) != OK)
			continue;
		g->khz[g->n++] = cl->c.khz[i];
	}
	g->cur = 0;
	g->off_point = 1;
	if (cf_state(cf, cl, &st) == OK) {
		for (i = 0; st.on_pll && i < g->n; i++)
			if (g->khz[i] <= st.khz)
				g->cur = i;
		g->off_point = st.on_pll ? g->khz[g->cur] != st.khz :
		    (st.mode & 0x7) != 0;	/* on aux, the PLL to be parked */
	}
	g->low = 0;
	return OK;
}

/* The point this period asks for: an index of g->khz. */
unsigned
cf_gov_pick(const struct cf_govpol *p, struct cf_govcl *g, unsigned load)
{
	unsigned top = g->n - 1, want;
	uint64_t need;

	switch (p->policy) {
	case CF_GOV_PERFORMANCE:
		g->low = 0;
		return top;
	case CF_GOV_POWERSAVE:
		g->low = 0;
		return 0;
	}
	if (load >= p->up)
		want = top;
	else {
		/* The lowest point at which this much work is aim% or less. */
		need = (uint64_t)g->khz[g->cur] * load;
		for (want = 0; want < top &&
		    (uint64_t)g->khz[want] * p->aim < need; want++)
			;
	}
	if (want >= g->cur) {
		g->low = 0;
		return want;
	}
	if (++g->low < p->downs)
		return g->cur;
	g->low = 0;
	return want;
}

/* Where the cluster is among its points, from the mux and the PLL. */
static void
resync(const struct cf *cf, const struct cf_cluster *cl, struct cf_govcl *g)
{
	struct cf_state st;
	unsigned i;

	g->cur = 0;
	g->off_point = 1;
	if (cf_state(cf, cl, &st) != OK)
		return;
	for (i = 0; st.on_pll && i < g->n; i++)
		if (g->khz[i] <= st.khz)
			g->cur = i;
	g->off_point = st.on_pll ? g->khz[g->cur] != st.khz :
	    (st.mode & 0x7) != 0;
}

/* The cluster to point i; on a failure, found again. */
static int
go(struct cf *cf, struct cf_cluster *cl, struct cf_govcl *g, unsigned i)
{
	uint32_t khz = i == 0 ? 0 : g->khz[i];
	int r;

	if ((r = cf_go(cf, cl, khz)) == OK) {
		if (i == 0)
			(void)cf_park(cf, cl);
		log_debug(&cf_log, "%s: %u.%u -> %u.%u MHz\n", cl->name,
		    MHZ(g->khz[g->cur]), MHZ(g->khz[i]));
		g->switches += g->cur != i || i != 0;
		g->cur = i;
		g->off_point = 0;
		g->fails = 0;
		return OK;
	}
	/*
	 * Where it is now: a refusal (EPERM - the rails do not hold what was
	 * asked, or no longer what it ran at) makes the points again from
	 * what they hold; anything else - a read of the arbiter that gave no
	 * whole answer - only finds the cluster among the points it has, from
	 * its mux and PLL alone.  Either way it is where cf_go() left it: at
	 * the old point, the new one, or aux.
	 */
	if (r != EPERM || cf_gov_points(cf, cl, g) != OK)
		resync(cf, cl, g);
	if (++g->fails >= FAILS_TO_FREEZE) {
		g->frozen = 1;
		log_warn(&cf_log, "%s: the governor leaves it where it is (%d)\n",
		    cl->name, r);
	}
	return r;
}

int
cf_gov_start(struct cf *cf, struct cf_cluster *cl, struct cf_govcl *g,
	const struct cf_govpol *p)
{
	unsigned i;
	int r;

	if ((r = cf_gov_points(cf, cl, g)) != OK) {
		g->frozen = 1;
		return r;
	}
	for (i = 0; i < g->n; i++)
		log_info(&cf_log, "%s: point %u: %u.%u MHz%s\n", cl->name, i,
		    MHZ(g->khz[i]), i == 0 ? " (aux)" : "");
	/* Ondemand starts at the top: the boot goes on, and it is busy. */
	i = p->policy == CF_GOV_POWERSAVE ? 0 : g->n - 1;
	if (i == g->cur && !g->off_point)
		return OK;
	return go(cf, cl, g, i);
}

/* One period: the time at the point counted, then the next point. */
int
cf_gov_step(struct cf *cf, struct cf_cluster *cl, struct cf_govcl *g,
	const struct cf_govpol *p, unsigned load, unsigned ticks)
{
	unsigned want;

	if (g->frozen || g->n == 0)
		return OK;
	g->res[g->cur] += ticks;
	want = cf_gov_pick(p, g, load);
	if (want == g->cur && !g->off_point)
		return OK;
	return go(cf, cl, g, want);
}

/*
 * Now and then, without a change: does what the cluster runs at still
 * hold?  MX is voted by others and may come down under a point that sits
 * still; cf_go() to the same point would not notice, so ask here.  If not,
 * the points are made again and the cluster goes to the highest of them
 * below where it was - by way of aux: cf_go() takes a cluster whose point
 * is no longer held there first, and refuses the rest of the move, so the
 * next period's decision finishes it.
 */
int
cf_gov_check(struct cf *cf, struct cf_cluster *cl, struct cf_govcl *g)
{
	int r;

	/* A read with no whole answer says nothing: the next check asks. */
	if (g->frozen || g->n == 0 || g->off_point ||
	    (r = cf_held(cf, cl, g->khz[g->cur], 1)) != EPERM)
		return OK;
	log_warn(&cf_log, "%s: the rails no longer hold %u.%u MHz\n", cl->name,
	    MHZ(g->khz[g->cur]));
	if (cf_gov_points(cf, cl, g) != OK) {
		resync(cf, cl, g);
		if (++g->fails >= FAILS_TO_FREEZE)
			g->frozen = 1;
		return r;
	}
	return go(cf, cl, g, g->cur);
}

/* Where the time went: one line per cluster. */
void
cf_gov_report(const struct cf_cluster *cl, const struct cf_govcl *g)
{
	char line[256];
	uint64_t all = 0;
	unsigned i;
	int n;

	for (i = 0; i < g->n; i++)
		all += g->res[i];
	n = snprintf(line, sizeof(line), "%s: %u switches;", cl->name,
	    g->switches);
	for (i = 0; i < g->n && n > 0 && (size_t)n < sizeof(line); i++)
		n += snprintf(line + n, sizeof(line) - (size_t)n,
		    " %u.%u %u%%", MHZ(g->khz[i]), all ? (unsigned)(g->res[i] *
		    100 / all) : 0);
	log_info(&cf_log, "%s%s\n", line, g->frozen ? " (frozen)" : "");
}
