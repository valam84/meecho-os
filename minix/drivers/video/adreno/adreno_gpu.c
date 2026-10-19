/*
 * The GPU kept up for the render node (port/clover/hw-gpu.md, step 7): what
 * the stages bring up and put back once (adreno_run.c) brought up for good,
 * and the ring fed with the clients' jobs.
 *
 * A job goes into the ring in the form of Linux's a5xx_submit() for a
 * single ring, without preemption (a5xx_gpu.c, GPL - the order and the
 * values, not the code; hw-gpu.md, §7):
 *
 *   CP_SET_PROTECTED_MODE 0; CP_CONTEXT_SWITCH_SAVE_ADDR 0 (no preemption
 *   record: one ring); CP_SET_PROTECTED_MODE 1;
 *   CP_INDIRECT_BUFFER_PFE per command buffer (an IB target is reached
 *   from another and not put here; a context-restore buffer only when the
 *   ring's last job was another client's);
 *   CP_SCRATCH_REG(2) = the job's fence;
 *   CP_EVENT_WRITE CACHE_FLUSH_TS with an interrupt: the caches written
 *   back, the fence to memory, the GPU's line.
 *
 * WHERE_AM_I goes after each job (cp_kick()), not every 32 buffers as
 * Linux's: that one is for Linux's ring space, read from the shadow; here
 * the space is known from the fences - a job whose fence is in memory has
 * been read past.
 *
 * Each client has an address space of its own (adreno_drm.c), as on the
 * CB2's Mali, where Linux on this GPU has one for everybody: TTBR1 is
 * forbidden on this SoC, and the ring cannot switch tables itself without
 * one (hw-gpu.md, §4).  The service switches them instead, between jobs:
 * a job of another client waits until the ring is empty and the GPU
 * idle, bank 0 is given the client's table (smmu_load(): TTBR0, TLB
 * invalidated), and the job begins by invalidating the GPU's UCHE - what
 * Mesa's own fd5_cache_flush() writes - so that nothing the last client
 * read is found at the same address by the next one.  The service's
 * buffers are in every table at the same address.
 *
 * The GPU's line (332) brings the fences: every job up to the one whose
 * fence memory holds is over.  Bank 0's line (361) brings a fault in a
 * client's space: the access was terminated (the bank does not stall),
 * the GPU goes on, and the job that ran - the first in the ring - fails
 * when it ends.  A job that makes no progress for hang= ms (2000; the GPU
 * runs at 19.2 MHz yet) is a hang, as is the GPU's own hang detection:
 * every job in the ring fails, the GPU is powered off and brought up again
 * as at the start (the zap shader resumed, not loaded), and the jobs that
 * waited go on.
 *
 * The core's clock follows the work (gov=, hw-gpu.md, the governor's
 * section): the points are the tree's operating points the bin allows, up
 * to freq= (a cap now, "max" by default).  VDDMX is voted once, for the
 * top point, and kept: the CPU clusters' memory arrays are on MX too (the
 * APM in MX mode, hw-cpu.md), so the GPU going idle must not take MX from
 * under them.  What a lower point saves is the GPU's clock itself.  With
 * sleep=MS the GPU is powered off - the GDSCs, the clocks, the PLLs, as
 * at the service's end - once nothing ran for that long, and brought up
 * again as after a hang (the zap shader resumed) by the next job.
 */

#include "adreno_drm.h"
#include "adreno_regs.h"

#include <minix/qcom_rpm.h>
#include <minix/sysutil.h>
#include <minix/timers.h>

#include <stdlib.h>
#include <string.h>

/* Registers and packets beside those of the stages (a5xx.xml, pm4.xml). */
#define A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR	0x083e	/* and _HI 0x083f */
#define A5XX_UCHE_CACHE_INVALIDATE_MIN		0x0e91	/* MIN, MAX, then */
#define UCHE_INVALIDATE_ALL			0x12	/* 0x0e95: fd5's */
#define CP_INDIRECT_BUFFER_PFE			0x3f
#define CP_WAIT_FOR_IDLE			0x26

/* What the GPU's line reports and the service does not wait out: the
 * GPU's own hang detection - the rest is logged (a5xx_irq()). */
#define INT_ERRORS	(0x7eu | A5XX_INT_CP_HW_ERROR | (1u << 24))

unsigned gpu_global_faults;
long ad_hang_ms = 2000;
unsigned ad_freq_mhz;
int ad_gov = AD_GOV_ONDEMAND;
unsigned ad_gov_ms = 50, ad_gov_up = 80, ad_gov_aim = 60, ad_gov_downs = 2;
long ad_sleep_ms;
long ad_report_s;

static TAILQ_HEAD(, ad_job) pending = TAILQ_HEAD_INITIALIZER(pending);
static TAILQ_HEAD(, ad_job) flight = TAILQ_HEAD_INITIALIZER(flight);
static struct ad_vm *on_ring;		/* whose table bank 0 walks; NULL: ours */
static struct ad_vm *last_vm;		/* the ring's last job's client */
static int inval_due;			/* the next job begins with UCHE's */
static uint32_t tail;			/* the CP is past this, in dwords */
static int up;				/* taking jobs into the ring */
static int dead;			/* did not come back from a hang */
static minix_timer_t hang_timer, retry_timer;
static int hang_armed, retry_armed;
static uint32_t hang_fence;		/* the fence at the last look */
static unsigned hang_looks;
static unsigned quiet_looks;		/* a switch of tables waiting: looks */

static void kick(void);
static void give_up(void);
static int quiesce(unsigned us);
static int gpu_wake(void);
static void gov_busy(int busy);
static void gov_arm(void);

/*
 * The core's clock while serving (freq=, hw-gpu.md, §20).
 *
 * An operating point of the tree (opp-hz) that the chip's speed bin allows
 * (opp-supported-hw against the bit Linux's catalog gives the fuse:
 * a5xx_catalog.c, A512) - without a fuse to read, only the points every
 * bin has (bit 0).  freq=max: the highest such point.
 *
 * VDDMX is voted for the point's level (opp-level) through qcom_rpm, in
 * Linux's order (the OPP core): the domain on and up before the clock goes
 * up, down after it comes down.  Without qcom_rpm only the boot's MIN_SVS
 * points are taken - the rail is at least that, or nothing would run.
 */
#define LEVEL_UNVOTED	0
static unsigned mx_voted = LEVEL_UNVOTED;	/* the level asked for */
static int mx_on;				/* "swen" 1 sent */
static uint32_t bin_hw;				/* opp-supported-hw bit */

static int
mx_vote(unsigned level)
{
	int r;

	if (!mx_on) {
		if ((r = ad_rpm_set(QRPM_KEY_ENABLE, 1)) != OK)
			return r;
		mx_on = 1;
	}
	if ((r = ad_rpm_set(QRPM_KEY_LEVEL, level)) != OK)
		return r;
	log_info(&ad_log, "Q25 vddmx: level %u voted (was %u)\n", level,
	    mx_voted);
	mx_voted = level;
	return OK;
}

/* The GPU off: the vote taken back, as rpmpd_power_off() does. */
static void
mx_release(void)
{
	if (!mx_on)
		return;
	if (ad_rpm_set(QRPM_KEY_ENABLE, 0) == OK)
		log_info(&ad_log, "Q25 vddmx: the vote taken back\n");
	mx_on = 0;
	mx_voted = LEVEL_UNVOTED;
}

/* Linux's speedbins for the A512 (a5xx_catalog.c): fuse -> bit. */
static uint32_t
bin_bit(int known, unsigned fuse)
{
	static const struct { unsigned fuse, bit; } sb[] = {
		{ 0, 6 }, { 78, 1 }, { 90, 2 }, { 122, 3 }, { 135, 4 },
		{ 146, 5 }, { 157, 6 },
	};
	unsigned i;

	if (known)
		for (i = 0; i < sizeof(sb) / sizeof(sb[0]); i++)
			if (sb[i].fuse == fuse)
				return 1u << sb[i].bit;
	return 1u << 0;		/* not known: what every bin has */
}

/* Q26: the bin, and the points it leaves. */
static void
bin_report(void)
{
	unsigned i, n = ad.dt.nopp < AD_MAXOPP ? ad.dt.nopp : AD_MAXOPP;
	unsigned fuse = 0, top = 0;
	int r = adreno_speed_bin(&fuse);

	bin_hw = bin_bit(r == OK, fuse);
	for (i = 0; i < n; i++)
		if ((ad.dt.opp[i].hw & bin_hw) && ad.dt.opp[i].mhz > top)
			top = ad.dt.opp[i].mhz;
	if (r == OK)
		log_info(&ad_log, "Q26 speed bin: fuse %u, opp-supported-hw "
		    "0x%x: points up to %u MHz\n", fuse, bin_hw, top);
	else
		log_info(&ad_log, "Q26 speed bin: no fuse to read (%d); the "
		    "points every bin has, up to %u MHz\n", r, top);
}

/* The point freq= names - "max" (1) the highest the bin allows - or -1. */
static int
core_point(void)
{
	unsigned i, n = ad.dt.nopp < AD_MAXOPP ? ad.dt.nopp : AD_MAXOPP;
	int best = -1;

	for (i = 0; i < n; i++) {
		if (!(ad.dt.opp[i].hw & bin_hw))
			continue;
		if (ad_freq_mhz == 1 ? (best < 0 ||
		    ad.dt.opp[i].mhz > ad.dt.opp[best].mhz) :
		    ad.dt.opp[i].mhz == ad_freq_mhz)
			best = (int)i;
	}
	return best;
}

/*===========================================================================*
 *    the governor's points                                                  *
 *===========================================================================*/
static unsigned pt_mhz[AD_MAXOPP];	/* ascending: the points it moves on */
static unsigned npts, pt_cur;		/* npts 0: no governor, one clock */
static unsigned gov_low;		/* periods in a row that asked lower */
static minix_timer_t gov_timer;
static int gov_armed, asleep;
static u64_t busy_since, busy_acc, win_start;
static int busy_on, ran;		/* jobs in the ring; any this period */
static long idle_ms;
static unsigned gov_switches, gov_sleeps;
static u64_t pt_res[AD_MAXOPP];		/* time at each point, frclock */
static u64_t asleep_res;		/* time powered off */
static minix_timer_t report_timer;

/*
 * The points: every operating point the bin allows, no higher than the
 * one freq= named (that one included; max: all), in ascending order.
 */
static void
gov_points(int top)
{
	unsigned i, n = ad.dt.nopp < AD_MAXOPP ? ad.dt.nopp : AD_MAXOPP, k, t;

	npts = 0;
	for (i = 0; i < n; i++) {
		if (!(ad.dt.opp[i].hw & bin_hw) || ad.dt.opp[i].mhz < 30 ||
		    ad.dt.opp[i].mhz > ad.dt.opp[top].mhz)
			continue;
		for (k = 0; k < npts && pt_mhz[k] != ad.dt.opp[i].mhz; k++)
			;
		if (k == npts)
			pt_mhz[npts++] = ad.dt.opp[i].mhz;
	}
	for (i = 1; i < npts; i++)
		for (k = i; k > 0 && pt_mhz[k - 1] > pt_mhz[k]; k--) {
			t = pt_mhz[k];
			pt_mhz[k] = pt_mhz[k - 1];
			pt_mhz[k - 1] = t;
		}
}

/* The clock to a point the vote already covers; its own log line quiet. */
static int
gov_set(unsigned i)
{
	int save = ad_log.log_level, r;

	if (save == LEVEL_INFO)
		ad_log.log_level = LEVEL_WARN;
	r = cc_gfx3d_set(pt_mhz[i]);
	ad_log.log_level = save;
	if (r != OK)
		return r;
	log_debug(&ad_log, "gpu: %u -> %u MHz\n", pt_mhz[pt_cur], pt_mhz[i]);
	gov_switches += pt_cur != i;
	pt_cur = i;
	return OK;
}

static void
core_clock(void)
{
	unsigned level, mhz, was = cc_gfx3d_mhz();
	int i, r;

	if (ad_freq_mhz == 0 || (ad_freq_mhz > 1 && ad_freq_mhz < 30))
		return;
	if ((i = core_point()) < 0) {
		log_warn(&ad_log, "gpu: freq=%u is not an operating point the "
		    "tree gives this bin; 19.2 MHz\n", ad_freq_mhz);
		return;
	}
	mhz = ad.dt.opp[i].mhz;
	level = ad.dt.opp[i].level;
	if (npts == 0)
		gov_points(i);
	/* The governor's start: the top, or for powersave the lowest. */
	if (npts > 0 && ad_gov == AD_GOV_POWERSAVE)
		mhz = pt_mhz[0];
	for (pt_cur = 0; npts > 0 && pt_mhz[pt_cur] != mhz; pt_cur++)
		;

	/* Up: the rail first. */
	if (level > mx_voted && (r = mx_vote(level)) != OK &&
	    level > RPM_LEVEL_MIN_SVS) {
		log_warn(&ad_log, "gpu: %u MHz wants VDDMX at level %u and the "
		    "vote did not go (%d); 19.2 MHz\n", mhz, level, r);
		return;
	}
	if (cc_gfx3d_set(mhz) != OK) {
		log_warn(&ad_log, "gpu: the core's clock stays at %u MHz\n",
		    cc_gfx3d_mhz());
		npts = 0;		/* no governor on a clock that would not go */
		return;
	}
	/* Down: the rail after. */
	if (mx_on && level < mx_voted && mhz < was)
		(void)mx_vote(level);
}

/*===========================================================================*
 *    the governor                                                           *
 *===========================================================================*/
/* The ring went busy or idle: what the period's load is made of. */
static void
gov_busy(int busy)
{
	u64_t now;

	if (busy == busy_on)
		return;
	read_frclock_64(&now);
	if (busy) {
		busy_since = now;
		ran = 1;
		idle_ms = 0;
	} else
		busy_acc += delta_frclock_64(busy_since, now);
	busy_on = busy;
}

static void gov_fired(int arg);

static void
gov_arm(void)
{
	clock_t t;
	u64_t now;

	/* Periods only for a load to follow or a sleep to wait for. */
	if (gov_armed || asleep || dead || (ad_sleep_ms == 0 &&
	    (npts < 2 || ad_gov != AD_GOV_ONDEMAND)))
		return;
	t = (clock_t)ad_gov_ms * sys_hz() / 1000;
	/* A new window: the rest before it was spent at this point. */
	read_frclock_64(&now);
	if (win_start != 0 && npts > 0)
		pt_res[pt_cur] += delta_frclock_64(win_start, now);
	win_start = now;
	busy_acc = 0;
	if (busy_on)
		busy_since = now;
	set_timer(&gov_timer, t > 0 ? t : 1, gov_fired, 0);
	gov_armed = 1;
}

/* The point this period's load asks for (as qcom_cpufreq's ondemand). */
static unsigned
gov_pick(unsigned load)
{
	unsigned top = npts - 1, want;

	if (ad_gov == AD_GOV_PERFORMANCE)
		return top;
	if (ad_gov == AD_GOV_POWERSAVE)
		return 0;
	if (load >= ad_gov_up)
		want = top;
	else
		for (want = 0; want < top && pt_mhz[want] * ad_gov_aim <
		    pt_mhz[pt_cur] * load; want++)
			;
	if (want >= pt_cur) {
		gov_low = 0;
		return want;
	}
	if (++gov_low < ad_gov_downs)
		return pt_cur;
	gov_low = 0;
	return want;
}

/* The GPU off: nothing in the ring and nothing waiting (gov_fired()). */
static int
gpu_sleep(void)
{
	int save = ad_log.log_level;
	u64_t now;

	/*
	 * Bank 0 back on the service's own table first, while it can still
	 * be told: a client may go while the GPU sleeps, and its table must
	 * not be the one a bank left powered would walk.
	 */
	if (!quiesce(1000))
		return 0;		/* not idle after all: next period */
	(void)smmu_load(NULL);
	on_ring = NULL;
	if (cp_halt() != OK)
		log_warn(&ad_log, "gpu: the VBIF's ports did not halt\n");
	if (save == LEVEL_INFO)
		ad_log.log_level = LEVEL_WARN;
	cc_power_down();
	ad_log.log_level = save;
	read_frclock_64(&now);
	if (win_start != 0 && npts > 0)
		pt_res[pt_cur] += delta_frclock_64(win_start, now);
	win_start = now;		/* from here: asleep_res */
	up = 0;
	asleep = 1;
	gov_sleeps++;
	log_debug(&ad_log, "gpu: asleep\n");
	return 1;
}

static void
gov_fired(int UNUSED(arg))
{
	u64_t now, win, busy;
	unsigned load, want;

	gov_armed = 0;
	if (!up || asleep)
		return;
	read_frclock_64(&now);
	win = delta_frclock_64(win_start, now);
	busy = busy_acc + (busy_on ? delta_frclock_64(busy_since, now) : 0);
	if (busy_on)
		busy_since = now;
	busy_acc = 0;
	win_start = now;
	load = win > 0 ? (unsigned)(busy * 100 / win) : 0;
	if (load > 100)
		load = 100;
	if (npts > 0)
		pt_res[pt_cur] += win;

	if (npts > 1) {
		want = gov_pick(load);
		if (want != pt_cur && gov_set(want) != OK)
			npts = 0;	/* the clock stays; so does the governor */
	}
	if (!ran && !busy_on && TAILQ_EMPTY(&flight) && TAILQ_EMPTY(&pending))
		idle_ms += (long)ad_gov_ms;
	ran = 0;
	if (ad_sleep_ms > 0 && idle_ms >= ad_sleep_ms &&
	    TAILQ_EMPTY(&flight) && TAILQ_EMPTY(&pending)) {
		if (gpu_sleep())
			return;
	}
	/* At rest at the lowest point with no sleep to wait for: no more
	 * periods until a job comes. */
	if (idle_ms > 0 && (npts < 2 || pt_cur == 0) && ad_sleep_ms == 0)
		return;
	gov_arm();
}

/* The GPU on again for a job, as after a hang (recover()). */
static int
gpu_wake(void)
{
	int save = ad_log.log_level, r;
	u64_t now;

	if (save == LEVEL_INFO)
		ad_log.log_level = LEVEL_WARN;
	if ((r = cc_power_up(AD_CP)) == OK && npts > 0)
		r = cc_gfx3d_set(pt_mhz[pt_cur]);
	/*
	 * The bank as it was, and its TLB forgotten whatever the power did to
	 * it: tables changed while the GPU slept, and nothing invalidated.
	 */
	if (r == OK && (r = smmu_check()) == OK &&
	    (r = smmu_load(smmu_loaded())) == OK)
		r = cp_boot();
	ad_log.log_level = save;
	asleep = 0;
	if (r != OK) {
		log_warn(&ad_log, "gpu: did not wake: %d; every job fails from "
		    "now on\n", r);
		give_up();
		return r;
	}
	(void)cp_irq();
	if (smmu_loaded() == NULL)
		on_ring = NULL;
	last_vm = NULL;
	inval_due = 1;
	tail = cp_wptr();
	up = 1;
	idle_ms = 0;
	read_frclock_64(&now);
	asleep_res += delta_frclock_64(win_start, now);
	win_start = now;
	log_debug(&ad_log, "gpu: awake\n");
	return OK;
}

/* For a reader of the GPU's registers outside a job (adreno_drm.c). */
int
gpu_awake(void)
{
	if (dead)
		return EIO;
	if (asleep && gpu_wake() != OK)
		return EIO;
	gov_arm();
	return OK;
}

/*
 * Where the time went - each point's share, and off - and the clock now as
 * the registers give it (cc_gfx3d_read_khz(); asleep, it is not read).
 */
void
gpu_status(char *line, size_t size)
{
	u64_t all = 0, now, open = 0, res[AD_MAXOPP], off = asleep_res;
	unsigned i;
	int n;

	if (npts == 0) {
		snprintf(line, size, "gpu: no governor; %u MHz", cc_gfx3d_mhz());
		return;
	}
	read_frclock_64(&now);
	if (win_start != 0)
		open = delta_frclock_64(win_start, now);
	for (i = 0; i < npts; i++)
		res[i] = pt_res[i] + (!asleep && i == pt_cur ? open : 0);
	if (asleep)
		off += open;
	for (i = 0; i < npts; i++)
		all += res[i];
	all += off;
	n = snprintf(line, size, "gpu: %u switches, %u sleeps;",
	    gov_switches, gov_sleeps);
	for (i = 0; i < npts && n > 0 && (size_t)n < size; i++)
		n += snprintf(line + n, size - (size_t)n, " %u %u%%",
		    pt_mhz[i], all ? (unsigned)(res[i] * 100 / all) : 0);
	if (n > 0 && (size_t)n < size && (ad_sleep_ms > 0 || off > 0))
		n += snprintf(line + n, size - (size_t)n, " off %u%%",
		    all ? (unsigned)(off * 100 / all) : 0);
	if (n > 0 && (size_t)n < size) {
		if (asleep)
			snprintf(line + n, size - (size_t)n, "; now off");
		else
			snprintf(line + n, size - (size_t)n, "; now %u kHz by the "
			    "registers", cc_gfx3d_read_khz());
	}
}

static void
gov_report(void)
{
	char line[256];

	if (npts == 0)
		return;
	gpu_status(line, sizeof(line));
	log_info(&ad_log, "%s\n", line);
}

/*
 * The policy changed while serving (sysctl minix.gpu, adreno.c): from now
 * on, at once - performance to the top point, powersave to the lowest,
 * ondemand from where it is; a sleep of 0 wakes a GPU that sleeps.
 */
int
gpu_policy(int gov, long sleep_ms)
{
	unsigned want;

	if (gov < AD_GOV_PERFORMANCE || gov > AD_GOV_ONDEMAND || sleep_ms < 0)
		return EINVAL;
	ad_gov = gov;
	ad_sleep_ms = sleep_ms;
	idle_ms = 0;
	gov_low = 0;
	if (dead)
		return OK;
	if (asleep) {
		if (sleep_ms == 0 && gpu_wake() != OK)
			return EIO;
	} else if (up && npts > 1) {
		want = gov == AD_GOV_PERFORMANCE ? npts - 1 :
		    gov == AD_GOV_POWERSAVE ? 0 : pt_cur;
		if (want != pt_cur && gov_set(want) != OK)
			return EIO;
	}
	gov_arm();
	return OK;
}

static void
report_fired(int UNUSED(arg))
{
	gov_report();
	if (ad_report_s > 0)
		set_timer(&report_timer, (clock_t)ad_report_s * sys_hz(),
		    report_fired, 0);
}

/*===========================================================================*
 *    the ring                                                               *
 *===========================================================================*/
static uint32_t
ring_free(void)
{
	uint32_t n = cp_ring_dwords();

	return (tail + n - cp_wptr() - 1) % n;
}

/* Words a job takes in the ring, its kick included. */
static uint32_t
job_dwords(const struct ad_job *j, int inval)
{
	return 2 + 3 + 2 + (inval ? 6 + 1 : 0) + 4 * j->ncmds + 2 + 5 +
	    cp_kick_dwords();
}

int
gpu_dead(void)
{
	return dead;
}

/* The largest job the ring takes (adreno_drm.c refuses bigger ones). */
uint32_t
gpu_max_cmds(void)
{
	return (cp_ring_dwords() - 64) / 4;
}

static void
emit(struct ad_job *j)
{
	uint64_t fence = cp_fence_va();
	int restore = last_vm != j->vm;
	unsigned i;

	cp_emit(pm4_pkt7(CP_SET_PROTECTED_MODE, 1));
	cp_emit(0);
	cp_emit(pm4_pkt4(A5XX_CP_CONTEXT_SWITCH_SAVE_ADDR, 2));
	cp_emit(0);
	cp_emit(0);
	cp_emit(pm4_pkt7(CP_SET_PROTECTED_MODE, 1));
	cp_emit(1);
	if (inval_due) {
		cp_emit(pm4_pkt4(A5XX_UCHE_CACHE_INVALIDATE_MIN, 5));
		cp_emit(0);
		cp_emit(0);
		cp_emit(0);
		cp_emit(0);
		cp_emit(UCHE_INVALIDATE_ALL);
		cp_emit(pm4_pkt7(CP_WAIT_FOR_IDLE, 0));
		inval_due = 0;
	}
	for (i = 0; i < j->ncmds; i++) {
		if (j->cmds[i].type == AD_CMD_TARGET ||
		    (j->cmds[i].type == AD_CMD_RESTORE && !restore))
			continue;
		cp_emit(pm4_pkt7(CP_INDIRECT_BUFFER_PFE, 3));
		cp_emit((uint32_t)j->cmds[i].iova);
		cp_emit((uint32_t)(j->cmds[i].iova >> 32));
		cp_emit(j->cmds[i].dwords);
	}
	cp_emit(pm4_pkt4(A5XX_CP_SCRATCH_REG(2), 1));
	cp_emit(j->seqno);
	cp_emit(pm4_pkt7(CP_EVENT_WRITE, 4));
	cp_emit(CP_EVENT_WRITE_IRQ | CACHE_FLUSH_TS);
	cp_emit((uint32_t)fence);
	cp_emit((uint32_t)(fence >> 32));
	cp_emit(j->seqno);
	cp_kick();
	j->end = cp_wptr();
}

/* A job out of the ring: its buffers to the processor, then its owner. */
static void
finish(struct ad_job *j)
{
	unsigned i;

	for (i = 0; i < j->nbos; i++) {
		j->bos[i]->mem->busy--;
		drmdrv_mem_for_cpu(j->bos[i]->mem);
	}
	j->done(j);
}

/* Look again in a tick: the GPU was not idle yet for a switch. */
static void
retry_fired(int UNUSED(arg))
{
	retry_armed = 0;
	kick();
}

static void
hang_fired(int arg);

static void
hang_arm(void)
{
	if (hang_armed)
		return;
	hang_fence = cp_fence();
	hang_looks = 0;
	set_timer(&hang_timer, (clock_t)(ad_hang_ms * (long)sys_hz() / 1000 +
	    1), hang_fired, 0);
	hang_armed = 1;
}

/*
 * The GPU idle - read past everything, nothing busy - polled a while: the
 * table switches only under that, and a table is freed only after it.
 */
static int
quiesce(unsigned us)
{
	unsigned t;

	for (t = 0; !cp_quiet(); t += 10) {
		if (t >= us)
			return 0;
		micro_delay(10);
	}
	return 1;
}

/*
 * Jobs that may go into the ring now go: in the order given, each after
 * the last if the ring has room, a client's after another's only once the
 * GPU is idle and the client's table loaded.
 */
static void
kick(void)
{
	struct ad_job *j;
	unsigned i;
	int r;

	while (up && (j = TAILQ_FIRST(&pending)) != NULL) {
		if (j->vm != on_ring) {
			if (!TAILQ_EMPTY(&flight))
				return;		/* after the other's last job */
			if (!quiesce(100)) {
				/* What keeps it from looking idle: once. */
				if (quiet_looks++ == 0)
					cp_say("not idle for a switch of tables");
				if (!retry_armed) {
					set_timer(&retry_timer, 1, retry_fired,
					    0);
					retry_armed = 1;
				}
				return;
			}
			if (quiet_looks > 0)
				log_info(&ad_log, "gpu: idle for a switch of "
				    "tables after %u looks\n", quiet_looks);
			quiet_looks = 0;
			if ((r = smmu_load(&j->vm->pt)) != OK) {
				log_warn(&ad_log, "gpu: %s's table not loaded: "
				    "%d\n", j->vm->name, r);
				TAILQ_REMOVE(&pending, j, link);
				j->result = EIO;
				j->done(j);
				continue;
			}
			on_ring = j->vm;
			inval_due = 1;
		}
		if (job_dwords(j, inval_due) > ring_free())
			return;			/* room comes with the fences */
		TAILQ_REMOVE(&pending, j, link);
		for (i = 0; i < j->nbos; i++) {
			j->bos[i]->mem->busy++;
			drmdrv_mem_for_gpu(j->bos[i]->mem);
		}
		j->seqno = cp_seq_next();
		emit(j);
		last_vm = j->vm;
		TAILQ_INSERT_TAIL(&flight, j, link);
		gov_busy(1);
		hang_arm();
	}
}

static void smmu_line(void);

/*
 * Every job whose fence memory holds is over; the room it took is free.  A
 * fault in bank 0 is looked for first: its line and the GPU's come
 * separately, and a job must not end well because the fence's line was
 * taken before the fault's.
 */
static void
retire(void)
{
	uint32_t fence;
	struct ad_job *j;

	smmu_line();
	fence = cp_fence();
	while ((j = TAILQ_FIRST(&flight)) != NULL &&
	    (int32_t)(fence - j->seqno) >= 0) {
		TAILQ_REMOVE(&flight, j, link);
		tail = j->end;
		finish(j);
	}
	if (TAILQ_EMPTY(&flight))
		gov_busy(0);
	kick();
}

/*===========================================================================*
 *    a hang                                                                 *
 *===========================================================================*/
/*
 * The GPU does not come back: every job fails, and the render node stays,
 * answering EIO, until the service is restarted.
 */
static void
give_up(void)
{
	struct ad_job *j;

	up = 0;
	dead = 1;
	while ((j = TAILQ_FIRST(&pending)) != NULL) {
		TAILQ_REMOVE(&pending, j, link);
		j->result = EIO;
		j->done(j);
	}
}

/*
 * Every job in the ring fails - the first is what hung - the GPU is
 * powered off and brought up again with the route and the tables as they
 * were, and the jobs that waited go on (Linux replays the innocent ones;
 * here they fail with the guilty, which is what a client's robustness
 * query is for).
 */
static void
recover(const char *why)
{
	struct ad_job *j;
	int r;

	cp_say(why);
	gpu_global_faults++;
	up = 0;
	if (hang_armed) {
		cancel_timer(&hang_timer);
		hang_armed = 0;
	}
	if (cp_halt() != OK)
		log_warn(&ad_log, "gpu: the VBIF's ports did not halt\n");
	while ((j = TAILQ_FIRST(&flight)) != NULL) {
		TAILQ_REMOVE(&flight, j, link);
		if (j->result == OK)
			j->result = EIO;
		finish(j);
	}
	gov_busy(0);
	cc_power_down();
	if ((r = cc_power_up(AD_CP)) == OK) {
		if (npts > 0)
			r = cc_gfx3d_set(pt_mhz[pt_cur]);
		else
			core_clock();
	}
	if (r != OK || (r = smmu_check()) != OK || (r = cp_boot()) != OK) {
		log_warn(&ad_log, "gpu: the GPU does not come back: %d; every "
		    "job fails from now on\n", r);
		give_up();
		return;
	}
	(void)cp_irq();
	if (smmu_loaded() == NULL)
		on_ring = NULL;		/* the bank was set up again */
	last_vm = NULL;
	inval_due = 1;			/* the GPU's caches started anew anyway */
	tail = cp_wptr();
	up = 1;
	log_warn(&ad_log, "gpu: up again after a hang (%u so far)\n",
	    gpu_global_faults);
	kick();
}

/*
 * The hang check (hw-gpu.md, §7: Linux's hangcheck): jobs in the ring and
 * the fence not moved since the last look - twice, so that a look just
 * after a job began is not counted.
 */
static void
hang_fired(int UNUSED(arg))
{
	uint32_t fence;

	hang_armed = 0;
	if (TAILQ_EMPTY(&flight) || !up)
		return;
	fence = cp_fence();
	if (fence != hang_fence) {
		hang_arm();
		return;
	}
	if (++hang_looks < 2) {
		set_timer(&hang_timer, (clock_t)(ad_hang_ms * (long)sys_hz() /
		    1000 + 1), hang_fired, 0);
		hang_armed = 1;
		return;
	}
	retire();
	if (!TAILQ_EMPTY(&flight))
		recover("gpu: a job made no progress");
}

/*===========================================================================*
 *    the lines                                                              *
 *===========================================================================*/
/* Bank 0: a fault in the space of the job that runs. */
static void
smmu_line(void)
{
	uint32_t fsr, fsynr0;
	uint64_t far;
	struct ad_job *j;

	if (!smmu_fault(&fsr, &far, &fsynr0))
		return;
	j = TAILQ_FIRST(&flight);
	if (j == NULL) {
		log_warn(&ad_log, "gpu: a fault at %llx (fsr %08x fsynr0 %08x) "
		    "with no job in the ring\n", (unsigned long long)far, fsr,
		    fsynr0);
		return;
	}
	j->vm->faults++;
	if (j->result == OK) {
		j->result = EIO;
		j->fault_fsr = fsr;
		j->fault_far = far;
		log_warn(&ad_log, "gpu: %s: a fault at %llx (fsr %08x fsynr0 "
		    "%08x), the access terminated; job %u fails\n", j->vm->name,
		    (unsigned long long)far, fsr, fsynr0, j->seqno);
	}
}

/* The GPU's: fences, and what went wrong. */
static void
gpu_line(void)
{
	uint32_t st;

	if (asleep)
		return;		/* not its line: the GPU is off */
	st = cp_irq();

	if (st & INT_ERRORS)
		cp_say("gpu: an error interrupt");
	if (st & A5XX_INT_MISC_HANG_DETECT) {
		retire();
		if (!TAILQ_EMPTY(&flight))
			recover("gpu: the GPU detected a hang");
		return;
	}
	retire();
}

void
gpu_intr(uint64_t mask)
{
	if (mask & (1ull << AD_SMMU_HOOK))
		smmu_line();
	if (mask & (1ull << AD_GPU_HOOK))
		gpu_line();
}

/*===========================================================================*
 *    for adreno_drm.c                                                       *
 *===========================================================================*/
/* Into the ring, after what is there and what waits for it. */
void
gpu_submit(struct ad_job *j)
{
	if (dead) {
		j->result = EIO;
		j->done(j);
		return;
	}
	if (asleep && gpu_wake() != OK) {
		j->result = EIO;
		j->done(j);
		return;
	}
	j->result = OK;
	TAILQ_INSERT_TAIL(&pending, j, link);
	gov_arm();
	kick();
}

/* A client closed: its jobs not yet in the ring never get there. */
void
gpu_cancel(struct ad_vm *vm)
{
	struct ad_job *j, *next;

	for (j = TAILQ_FIRST(&pending); j != NULL; j = next) {
		next = TAILQ_NEXT(j, link);
		if (j->vm == vm) {
			TAILQ_REMOVE(&pending, j, link);
			j->result = ECANCELED;
			j->done(j);
		}
	}
}

/*
 * A client's space goes, its last job over: if bank 0 walks its table, the
 * service's own goes there first - once the GPU is idle, so that no walk
 * is left in the table freed next.
 */
void
gpu_vm_gone(struct ad_vm *vm)
{
	if (last_vm == vm)
		last_vm = NULL;
	if (on_ring != vm)
		return;
	if (up && !quiesce(100000)) {
		log_warn(&ad_log, "gpu: not idle when %s's table goes\n",
		    vm->name);
		cp_say("not idle");
	}
	(void)smmu_load(NULL);
	on_ring = NULL;
	kick();
}

/*===========================================================================*
 *    up and down                                                            *
 *===========================================================================*/
int
gpu_serve(void)
{
	int r;

	init_timer(&hang_timer);
	init_timer(&retry_timer);
	init_timer(&gov_timer);
	init_timer(&report_timer);
	npts = 0;
	asleep = busy_on = 0;
	bin_report();
	if ((r = cc_power_up(AD_CP)) == OK)
		core_clock();
	if (r == OK && (r = smmu_serve()) == OK && (r = cp_alloc()) == OK &&
	    (r = cp_boot()) == OK)
		r = cp_first();
	if (r != OK) {
		cp_stop();
		smmu_undo();
		cc_power_down();
		mx_release();
		cp_free();
		return r;
	}
	/* The first command's line was taken: the kernel keeps it masked
	 * until the driver says so. */
	(void)cp_irq();
	tail = cp_wptr();
	on_ring = last_vm = NULL;
	up = 1;
	dead = 0;
	if (npts > 1 || ad_sleep_ms > 0) {
		static const char *const gname[] = { "performance",
		    "powersave", "ondemand" };
		unsigned i;
		char pts[64];
		int n = 0;

		for (i = 0; i < npts && n >= 0 && (size_t)n < sizeof(pts); i++)
			n += snprintf(pts + n, sizeof(pts) - (size_t)n, "%s%u",
			    i ? " " : "", pt_mhz[i]);
		log_info(&ad_log, "Q27 governor: %s, points %s MHz, every %u "
		    "ms; sleep %s\n", gname[ad_gov], npts ? pts : "none",
		    ad_gov_ms, ad_sleep_ms > 0 ? "after idle" : "never");
		if (ad_sleep_ms > 0)
			log_info(&ad_log, "Q27 sleep after %ld ms idle\n",
			    ad_sleep_ms);
	}
	gov_arm();
	if (ad_report_s > 0 && npts > 0)
		set_timer(&report_timer, (clock_t)ad_report_s * sys_hz(),
		    report_fired, 0);
	return OK;
}

/*
 * The service goes: whatever waits is cancelled, whatever runs fails (the
 * GPU is stopped under it), and then - adreno_drm_shutdown() having freed
 * every client's space - everything as found.
 */
void
gpu_stop(void)
{
	struct ad_job *j;

	up = 0;
	if (hang_armed)
		cancel_timer(&hang_timer);
	if (retry_armed)
		cancel_timer(&retry_timer);
	if (gov_armed)
		cancel_timer(&gov_timer);
	cancel_timer(&report_timer);
	hang_armed = retry_armed = gov_armed = 0;
	gov_report();
	/* Asleep: powered up once more, so that the rest goes down as it
	 * always does and finds everything as it expects. */
	if (asleep && gpu_wake() != OK)
		return;
	gov_busy(0);
	(void)cp_halt();
	while ((j = TAILQ_FIRST(&flight)) != NULL) {
		TAILQ_REMOVE(&flight, j, link);
		j->result = EIO;
		finish(j);
	}
	while ((j = TAILQ_FIRST(&pending)) != NULL) {
		TAILQ_REMOVE(&pending, j, link);
		j->result = ECANCELED;
		j->done(j);
	}
	(void)smmu_load(NULL);
	on_ring = NULL;
}

/* After adreno_drm_shutdown(): the rest down, back as found (Q23, Q17, Q11). */
void
gpu_down(void)
{
	cp_stop();
	smmu_undo();
	cc_power_down();
	mx_release();
	cp_free();
}
