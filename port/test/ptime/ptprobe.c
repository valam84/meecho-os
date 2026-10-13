/*
 * ptprobe - how much CPU time the kernel charges a process that runs in
 * short bursts, against what the process actually ran.
 *
 * Why.  On the board ps(1) showed the compositor 0:00.00 while its psinfo
 * cycles grew by seconds (PORTING-LOG.md, "Время процесса у событийных
 * программ").  The kernel has two clocks for a process: p_cycles, added up
 * from CNTVCT_EL0 on every crossing, and p_user_time/p_sys_time, which
 * the tick handler used to bump for whoever was running when the tick came.
 * This program is a process of the suspect kind with a known duty cycle,
 * and it reports both clocks plus where in the tick its bursts started.
 *
 *	ptprobe [-m busy|timer|pipe] [-w work_us] [-p period_us] [-t secs]
 *
 *   busy   spin for the whole window - the control: every clock must agree.
 *   timer  sleep in select(2) for period_us, then spin work_us; repeat.
 *          The wake-up comes from a kernel timer, and kernel timers expire
 *          in the boot core's tick handler: the burst starts right after a
 *          tick.
 *   pipe   the same burst, but woken by a byte from a child ("pacer") that
 *          spins on CNTVCT_EL0 for a random interval in [w + p/2, w + 3p/2]
 *          and writes.  The wake-up is at a random phase of the tick, so a
 *          sampling clock has no excuse to miss it - unless it is not the
 *          phase that makes it miss.  The pacer takes a core of its own.
 *
 * Output: one line of numbers, then the wake-phase histogram (tenths of a
 * tick, relative to CNTVCT_EL0 mod the tick length - an arbitrary but fixed
 * origin) and on which CPU the probe was found running.
 *
 * The probe reads its own psinfo (procfs; fields 8, 9 and 10 are user and
 * system ticks and cycles), so procfs must be mounted.  CNTVCT_EL0 is
 * readable at EL0 on this port (CNTKCTL_EL1.EL0VCTEN).
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <err.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NBINS	10
#define NCPUS	8

static inline uint64_t
cntvct(void)
{
	uint64_t v;

	__asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(v));
	return v;
}

static uint64_t
cntfrq(void)
{
	uint64_t v;

	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

struct pinfo {
	unsigned long ut, st;
	unsigned long long cyc, kipc, kcall;
};

static int
psinfo(pid_t pid, struct pinfo *pi)
{
	char path[64], name[64], type, state;
	int ver, ep, blk, prio, n;
	FILE *f;

	snprintf(path, sizeof(path), "/proc/%d/psinfo", (int)pid);
	if ((f = fopen(path, "r")) == NULL)
		return -1;
	n = fscanf(f, "%d %c %d %63s %c %d %d %lu %lu %llu %llu %llu",
	    &ver, &type, &ep, name, &state, &blk, &prio,
	    &pi->ut, &pi->st, &pi->cyc, &pi->kipc, &pi->kcall);
	fclose(f);
	return (n == 12) ? 0 : -1;
}

/* Which CPU this process is on - asked while it is running, so its own. */
static int
mycpu(void)
{
	struct kinfo_proc2 kp;
	size_t len = sizeof(kp);
	int mib[6];

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC2;
	mib[2] = KERN_PROC_PID;
	mib[3] = getpid();
	mib[4] = sizeof(kp);
	mib[5] = 1;
	if (sysctl(mib, 6, &kp, &len, NULL, 0) != 0 || len < sizeof(kp))
		return -1;
	return (int)kp.p_cpuid;
}

static void
spin(uint64_t counts)
{
	uint64_t t0 = cntvct();

	while (cntvct() - t0 < counts)
		;
}

static uint64_t
xorshift(uint64_t *s)
{
	uint64_t x = *s;

	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return *s = x;
}

static void __dead
pacer(int fd, uint64_t base, uint64_t range)
{
	uint64_t seed = cntvct() | 1;
	char c = 'x';

	for (;;) {
		spin(base + xorshift(&seed) % range);
		if (write(fd, &c, 1) != 1)
			_exit(0);
	}
}

static void
usage(void)
{
	fprintf(stderr, "usage: ptprobe [-m busy|timer|pipe] [-w work_us] "
	    "[-p period_us] [-t secs] [-c]\n");
	exit(2);
}

int
main(int argc, char **argv)
{
	const char *mode = "timer";
	long work_us = 3000, period_us = 10000, secs = 10;
	int ch, hz, nocpu = 0, pfd[2], i;
	uint64_t freq, tick, work, end, t0, ran = 0;
	unsigned long wakes = 0, hist[NBINS], cpus[NCPUS + 1];
	struct pinfo a, b;
	pid_t child = -1;
	double charged, cyc_s;

	while ((ch = getopt(argc, argv, "m:w:p:t:c")) != -1) {
		switch (ch) {
		case 'm': mode = optarg; break;
		case 'w': work_us = atol(optarg); break;
		case 'p': period_us = atol(optarg); break;
		case 't': secs = atol(optarg); break;
		case 'c': nocpu = 1; break;
		default: usage();
		}
	}
	if (strcmp(mode, "busy") && strcmp(mode, "timer") &&
	    strcmp(mode, "pipe"))
		usage();

	freq = cntfrq();
	if ((hz = (int)sysconf(_SC_CLK_TCK)) <= 0)
		hz = 100;
	tick = freq / (uint64_t)hz;
	work = freq * (uint64_t)work_us / 1000000;
	memset(hist, 0, sizeof(hist));
	memset(cpus, 0, sizeof(cpus));

	if (!strcmp(mode, "pipe")) {
		uint64_t per = freq * (uint64_t)period_us / 1000000;

		if (pipe(pfd) != 0)
			err(1, "pipe");
		if ((child = fork()) < 0)
			err(1, "fork");
		if (child == 0) {
			close(pfd[0]);
			pacer(pfd[1], work + per / 2, per);
		}
		close(pfd[1]);
	}

	/*
	 * mib refreshes its copy of the process tables at most once a tick,
	 * so a process exec'd within the current tick may not be in it yet.
	 */
	for (i = 0; psinfo(getpid(), &a) != 0; i++) {
		if (i == 10)
			errx(1, "cannot read /proc/%d/psinfo", (int)getpid());
		usleep(20000);
	}

	end = cntvct() + freq * (uint64_t)secs;
	if (!strcmp(mode, "busy")) {
		t0 = cntvct();
		spin(end - t0);
		ran = cntvct() - t0;
		wakes = 1;
	} else {
		while ((t0 = cntvct()) < end) {
			if (!strcmp(mode, "timer")) {
				struct timeval tv;

				tv.tv_sec = period_us / 1000000;
				tv.tv_usec = period_us % 1000000;
				(void)select(0, NULL, NULL, NULL, &tv);
			} else {
				char buf[64];

				if (read(pfd[0], buf, sizeof(buf)) <= 0)
					errx(1, "pacer went away");
			}
			t0 = cntvct();
			hist[(t0 % tick) * NBINS / tick]++;
			spin(work);
			ran += cntvct() - t0;
			/*
			 * Where the burst ran.  Asking costs a round trip to
			 * mib, which is itself a burst; once in fifty keeps it
			 * out of the measurement and still says which core.
			 */
			if (!nocpu && wakes % 50 == 0) {
				int c = mycpu();

				cpus[(c >= 0 && c < NCPUS) ? c : NCPUS]++;
			}
			wakes++;
		}
	}

	/*
	 * psinfo goes through mib, which refreshes its copy of the process
	 * table at most once a tick: give it one before reading again.
	 */
	if (psinfo(getpid(), &b) != 0)
		errx(1, "cannot read psinfo at the end");

	if (child > 0) {
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
	}

	charged = (double)((b.ut - a.ut) + (b.st - a.st)) / hz;
	cyc_s = (double)(b.cyc - a.cyc) / (double)freq;
	printf("ptprobe %s w=%ldus p=%ldus t=%lds hz=%d freq=%llu: "
	    "wakes %lu ran %.3f s cycles %.3f s charged %.2f s "
	    "(user %lu sys %lu ticks) kipc %.3f s kcall %.3f s "
	    "charged/cycles %.0f%%\n",
	    mode, work_us, period_us, secs, hz, (unsigned long long)freq,
	    wakes, (double)ran / freq, cyc_s, charged,
	    b.ut - a.ut, b.st - a.st,
	    (double)(b.kipc - a.kipc) / freq, (double)(b.kcall - a.kcall) / freq,
	    cyc_s > 0 ? 100.0 * charged / cyc_s : 0.0);
	if (strcmp(mode, "busy")) {
		printf("  wake phase:");
		for (i = 0; i < NBINS; i++)
			printf(" %lu", hist[i]);
		printf("\n");
	}
	if (!nocpu && strcmp(mode, "busy")) {
		printf("  on cpu:");
		for (i = 0; i < NCPUS; i++)
			if (cpus[i])
				printf(" %d:%lu", i, cpus[i]);
		if (cpus[NCPUS])
			printf(" ?:%lu", cpus[NCPUS]);
		printf("\n");
	}
	return 0;
}
