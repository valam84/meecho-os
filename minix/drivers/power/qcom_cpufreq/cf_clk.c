/*
 * One cluster's clock: its mux and its PLL (port/clover/hw-cpu.md,
 * sections 3, 8 and 9, path A).
 *
 * The mux is an RCG in the cluster's APCS GLB block (CMD_RCGR +0x50,
 * CFG_RCGR +0x54): source in bits 10:8 - 4 is GPLL0 "aux", 5 the
 * cluster's PLL, early output - and a half-integer divider in 4:0.  A new
 * CFG takes effect when CMD's UPDATE bit is set, and the RCG clears the bit
 * once it has switched (the vendor's OSM sequencer writes 0x3 and waits
 * for 0x2: RCG_UPDATE, RCG_UPDATE_SUCCESS).  The PLL's early output runs
 * at L times the 19.2 MHz crystal.
 *
 * What the boot loader leaves (section 8): silver on its PLL at L 0x48,
 * 1382.4 MHz; gold on aux, 600 MHz, its PLL idle.  A change of frequency
 * is always the same three moves, as section 9 lays out:
 *
 *   1. the mux on aux (CFG 0x401 + UPDATE), unless it is there already -
 *      the cores now run from GPLL0, and the PLL from nobody;
 *   2. the PLL programmed: MODE 0, L, MODE 2 (bypass), 10 us, MODE 6
 *      (reset released), STATUS bit 16 (locked), MODE 7 (output on) -
 *      clk_osm_setup_cluster_pll()'s order, with the boot loader's
 *      USER_CTL and CONFIG_CTL_LO kept as they are;
 *   3. the mux on the PLL's early output (CFG 0x501 + UPDATE).
 *
 * The undo is the mux back on aux.  Nothing else is ever written: not the
 * PLL's other registers, not the PMIC, not a SAW, not the APM.
 *
 * The guards, each one a reason to stop before a write:
 *   - the state must be one this file understands (cf_state()): a mux on
 *     aux or on a locked PLL, not divided, no switch pending;
 *   - before every move of the mux, the rail's set point is read back
 *     (cf_rail.c) and the point the cluster is about to run at - the
 *     target or aux - must be held by it: its open-loop voltage for this
 *     chip (cf_volt.c) no higher than the set point, and below 872 mV, the
 *     APM threshold, so the memory arrays may stay on VDD_MX where the
 *     boot loader left them;
 *   - the PLL is never touched while the mux is on it - read back just
 *     before the first write;
 *   - the mux goes to the PLL only once it says it is locked.
 */

#include "qcom_cpufreq.h"

#define RCG_CMD		0x50
#define RCG_CFG		0x54
#define CMD_UPDATE	(1u << 0)
#define CMD_ROOT_OFF	(1u << 31)
#define CFG_FIELDS	0x71fu		/* source 10:8, divider 4:0 */
#define CFG_SRC(v)	(((v) >> 8) & 7)
#define CFG_DIV(v)	((v) & 0x1f)
#define SRC_AUX		4		/* GPLL_SEL */
#define SRC_EARLY	5		/* PLL_EARLY_SEL */
#define CFG_AUX		0x401		/* aux, divider 1 */
#define CFG_EARLY	0x501		/* the PLL's early output, divider 1 */

#define PLL_MODE	0x00
#define PLL_L		0x04
#define PLL_USER_CTL	0x0c
#define PLL_CONFIG_LO	0x10
#define PLL_STATUS	0x2c
#define MODE_MASK	0x7
#define MODE_OFF	0x0
#define MODE_BYPASS	0x2		/* BYPASSNL */
#define MODE_RUN	0x6		/* BYPASSNL | RESET_N */
#define MODE_ON		0x7		/* and OUTCTRL */
#define STATUS_LOCK	(1u << 16)
#define USER_EARLY	(1u << 3)	/* the early output enabled */

#define PLL_MIN_L	43		/* PLL_MIN_LVAL: 825.6 MHz */
#define PLL_MAX_L	0x80		/* 2457.6 MHz, the top of any bin */

#define BYPASS_US	10		/* PLL_WAIT_LOCK_TIME_US */
#define LOCK_POLLS	500		/* of 1 us; the vendor waits 50-75 */
#define MUX_POLLS	1000		/* of 1 us */

#define MHZ(k)		(unsigned)((k) / 1000), (unsigned)((k) % 1000 / 100)

/* The L that makes this rate, or 0 if no point of a PLL makes it. */
uint32_t
cf_point_l(uint32_t khz)
{
	uint32_t l;

	if (khz == 0 || khz % CF_XO_KHZ != 0)
		return 0;
	l = khz / CF_XO_KHZ;
	return l >= PLL_MIN_L && l <= PLL_MAX_L ? l : 0;
}

/* What the cluster runs at now, if this file understands it. */
int
cf_state(const struct cf *cf, const struct cf_cluster *cl,
	struct cf_state *st)
{
	memset(st, 0, sizeof(*st));
	st->cmd = cf_rd(&cl->glb, RCG_CMD);
	st->cfg = cf_rd(&cl->glb, RCG_CFG);
	st->mode = cf_rd(&cl->pll, PLL_MODE);
	st->l = cf_rd(&cl->pll, PLL_L);
	st->user = cf_rd(&cl->pll, PLL_USER_CTL);
	st->config = cf_rd(&cl->pll, PLL_CONFIG_LO);
	st->status = cf_rd(&cl->pll, PLL_STATUS);

	if (st->cmd & CMD_ROOT_OFF)
		st->why = "the mux's root is off";
	else if (st->cmd & CMD_UPDATE)
		st->why = "a switch of the mux is pending";
	else if ((st->cfg & ~CFG_FIELDS) != 0 || CFG_DIV(st->cfg) > 1)
		st->why = "the mux divides, or has bits this service does "
		    "not set";
	else if (CFG_SRC(st->cfg) == SRC_AUX) {
		if (cf->aux_khz == 0)
			st->why = "the mux is on aux, and aux is not running";
		else
			st->khz = cf->aux_khz;
	} else if (CFG_SRC(st->cfg) == SRC_EARLY) {
		st->on_pll = 1;
		if ((st->mode & MODE_MASK) != MODE_ON ||
		    !(st->status & STATUS_LOCK))
			st->why = "the mux is on a PLL that is not locked";
		else if (!(st->user & USER_EARLY))
			st->why = "the mux is on the PLL's early output, "
			    "which is off";
		else if (st->l == 0 || st->l > PLL_MAX_L)
			st->why = "the PLL's L is out of any table";
		else
			st->khz = st->l * CF_XO_KHZ;
	} else
		st->why = "the mux is on a source this service does not use";
	return st->khz != 0 ? OK : EINVAL;
}

/*
 * Is this rate held at this set point?  Its corner's open-loop voltage no
 * higher than the set point, and below the APM threshold.
 */
static const char *
held_at(const struct cf_cluster *cl, uint32_t khz, unsigned mv,
	int32_t *uv)
{
	int i;

	*uv = 0;
	if (khz == 0)
		return "not running";	/* aux with its root off */
	if ((i = cf_corner_of(&cl->c, khz)) < 0)
		return "above every corner of this chip";
	*uv = cl->c.ol_uv[i];
	if (*uv >= CF_APM_UV)
		return "needs the arrays on VDD_APC (APM)";
	if (*uv > (int32_t)mv * 1000)
		return "needs more than the rail's set point";
	return NULL;
}

/* The same, with the rail's set point read back now. */
int
cf_held(struct cf *cf, const struct cf_cluster *cl, uint32_t khz, int quiet)
{
	const char *why;
	unsigned mv;
	int32_t uv;
	int r;

	if ((r = cf_rails_mv(cf, cl, &mv)) != OK) {
		log_warn(&cf_log, "%s: no set point read (%d); refusing\n",
		    cl->name, r);
		return r;
	}
	if ((why = held_at(cl, khz, mv, &uv)) == NULL)
		return OK;
	if (!quiet)
		log_warn(&cf_log, "%s: %u.%u MHz refused: open loop %d mV, "
		    "rail %u mV - %s\n", cl->name, MHZ(khz), (int)(uv / 1000),
		    mv, why);
	return EPERM;
}

/* The highest point of the table a PLL makes and the rail holds. */
int
cf_highest(struct cf *cf, const struct cf_cluster *cl, uint32_t *khz)
{
	unsigned mv;
	int32_t uv;
	int i, r;

	if ((r = cf_rails_mv(cf, cl, &mv)) != OK)
		return r;
	for (i = (int)cl->c.n - 1; i >= 0; i--)
		if (cf_point_l(cl->c.khz[i]) != 0 &&
		    held_at(cl, cl->c.khz[i], mv, &uv) == NULL) {
			*khz = cl->c.khz[i];
			return OK;
		}
	return ENOENT;
}

/* The mux to a new source, and the switch waited for. */
static int
mux(const struct cf_cluster *cl, uint32_t cfg)
{
	uint32_t got;
	unsigned i;

	cf_wr(&cl->glb, RCG_CFG, cfg);
	cf_wr(&cl->glb, RCG_CMD, cf_rd(&cl->glb, RCG_CMD) | CMD_UPDATE);
	for (i = 0; i < MUX_POLLS; i++) {
		if (!(cf_rd(&cl->glb, RCG_CMD) & CMD_UPDATE))
			break;
		cf_delay_us(1);
	}
	if (i == MUX_POLLS) {
		log_warn(&cf_log, "%s: the mux did not switch to 0x%x in "
		    "%u us; leaving it\n", cl->name, cfg, MUX_POLLS);
		return ETIMEDOUT;
	}
	if ((got = cf_rd(&cl->glb, RCG_CFG)) != cfg) {
		log_warn(&cf_log, "%s: the mux reads 0x%x, not 0x%x\n",
		    cl->name, got, cfg);
		return EIO;
	}
	return OK;
}

/*
 * The PLL to a new L, while the cores run from aux.  On a failure the PLL
 * is left off: nobody runs from it.
 */
static int
pll_program(const struct cf_cluster *cl, uint32_t l)
{
	uint32_t cfg, got;
	unsigned i;

	cfg = cf_rd(&cl->glb, RCG_CFG);
	if (CFG_SRC(cfg) != SRC_AUX ||
	    (cf_rd(&cl->glb, RCG_CMD) & CMD_UPDATE)) {
		log_warn(&cf_log, "%s: the mux (0x%x) is not settled on aux; "
		    "not touching the PLL\n", cl->name, cfg);
		return EPERM;
	}
	cf_wr(&cl->pll, PLL_MODE, MODE_OFF);
	cf_wr(&cl->pll, PLL_L, l);
	if ((got = cf_rd(&cl->pll, PLL_L)) != l) {
		log_warn(&cf_log, "%s: the PLL took L 0x%x as 0x%x; left "
		    "off\n", cl->name, l, got);
		return EIO;
	}
	cf_wr(&cl->pll, PLL_MODE, MODE_BYPASS);
	(void)cf_rd(&cl->pll, PLL_MODE);	/* there before the wait starts */
	cf_delay_us(BYPASS_US);
	cf_wr(&cl->pll, PLL_MODE, MODE_RUN);
	for (i = 0; i < LOCK_POLLS; i++) {
		if (cf_rd(&cl->pll, PLL_STATUS) & STATUS_LOCK)
			break;
		cf_delay_us(1);
	}
	if (i == LOCK_POLLS) {
		cf_wr(&cl->pll, PLL_MODE, MODE_OFF);
		log_warn(&cf_log, "%s: the PLL did not lock at L 0x%x in %u "
		    "us; left off\n", cl->name, l, LOCK_POLLS);
		return ETIMEDOUT;
	}
	cf_wr(&cl->pll, PLL_MODE, MODE_ON);
	if ((cf_rd(&cl->pll, PLL_MODE) & MODE_MASK) != MODE_ON ||
	    !(cf_rd(&cl->pll, PLL_STATUS) & STATUS_LOCK)) {
		cf_wr(&cl->pll, PLL_MODE, MODE_OFF);
		log_warn(&cf_log, "%s: the PLL lost its lock with the output "
		    "on; left off\n", cl->name);
		return EIO;
	}
	log_debug(&cf_log, "%s: PLL locked at L 0x%x after %u us\n", cl->name,
	    l, i);
	return OK;
}

/*
 * The cluster to one point: khz a point of its table that a PLL makes, or
 * 0 for aux.  Refuses, before any write, what the rail does not hold.
 */
int
cf_go(struct cf *cf, struct cf_cluster *cl, uint32_t khz)
{
	struct cf_state st;
	uint32_t l = 0;
	int r;

	if (cf_state(cf, cl, &st) != OK) {
		log_warn(&cf_log, "%s: %s; not touching it\n", cl->name,
		    st.why);
		return EINVAL;
	}
	if (khz != 0 && (l = cf_point_l(khz)) == 0) {
		log_warn(&cf_log, "%s: %u.%u MHz is not a point a PLL makes\n",
		    cl->name, MHZ(khz));
		return EINVAL;
	}

	/*
	 * What runs now must still be held - the rail is read anew at every
	 * step.  If it is not, the cluster goes down to aux, if the rail
	 * holds that, and goes no further.
	 */
	if ((r = cf_held(cf, cl, st.khz, 0)) != OK) {
		if (r == EPERM && st.on_pll &&
		    cf_held(cf, cl, cf->aux_khz, 0) == OK &&
		    mux(cl, CFG_AUX) == OK)
			log_warn(&cf_log, "%s: the rail no longer holds %u.%u "
			    "MHz: on aux (%u.%u MHz) now\n", cl->name,
			    MHZ(st.khz), MHZ(cf->aux_khz));
		return r;
	}

	/* aux is where the cores wait while the PLL is programmed. */
	if ((r = cf_held(cf, cl, cf->aux_khz, 0)) != OK ||
	    (l != 0 && (r = cf_held(cf, cl, khz, 0)) != OK))
		return r;
	if (l == 0)
		return st.on_pll ? mux(cl, CFG_AUX) : OK;
	if (st.on_pll && st.l == l)
		return OK;
	if (st.on_pll && (r = mux(cl, CFG_AUX)) != OK)
		return r;
	if ((r = pll_program(cl, l)) != OK) {
		log_warn(&cf_log, "%s: stays on aux, %u.%u MHz\n", cl->name,
		    MHZ(cf->aux_khz));
		return r;
	}

	/* Before the change itself, the rail once more. */
	if ((r = cf_held(cf, cl, khz, 0)) != OK) {
		log_warn(&cf_log, "%s: stays on aux, %u.%u MHz\n", cl->name,
		    MHZ(cf->aux_khz));
		return r;
	}
	if ((r = mux(cl, CFG_EARLY)) != OK)
		return r;
	if (cf_state(cf, cl, &st) != OK || st.khz != khz) {
		log_warn(&cf_log, "%s: after the switch: %s; back on aux\n",
		    cl->name, st.why != NULL ? st.why : "another rate");
		(void)mux(cl, CFG_AUX);
		return EIO;
	}
	return OK;
}
