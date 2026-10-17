/*
 * fdtest - a client of the adreno service on MEECHO: the msm DRM interface
 * (/dev/dri/renderD128) as Mesa's freedreno uses it, every answer checked
 * by the processor (port/clover/hw-gpu.md, step 7).
 *
 *	fdtest			all of the below, in order
 *	fdtest info		version, parameters, the timestamp running
 *	fdtest job		buffers, one job: CP_MEM_WRITE into a buffer,
 *				its fence by WAIT_FENCE, a sync file, the words
 *				read back
 *	fdtest fault		a write to an address nobody mapped: the job
 *				fails, FAULTS counts it, the next job is clean
 *	fdtest two		two contexts, one buffer address each: the
 *				GPU's tables switched, each sees its own words
 *	fdtest chain [n]	n jobs back to back (100), the time of each
 *
 * The command buffers are PM4 written here (the encoding of
 * adreno_pm4.xml, MIT): no Mesa, no microcode of anybody else's.  A line
 * per question; a line with "NOT" is a failure, and the exit status says
 * how many there were.
 *
 * Built by the cross compiler against DESTDIR, msm_drm.h from the tree
 * (the libdrm reachover's - DESTDIR may still hold libdrm's old one):
 *	aarch64-elf64-minix-gcc -O -static \
 *	    -I$TREE/external/mit/libdrm/lib/libdrm/drm \
 *	    -I$DEST/usr/include/libdrm -idirafter $DEST/usr/include/gfxcompat \
 *	    --sysroot=$DEST -o fdtest fdtest.c
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ioc_mmap.h>
#include <sys/mman.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "drm.h"
#include "msm_drm.h"
#include <linux/sync_file.h>		/* -idirafter .../gfxcompat */

#define NODE		"/dev/dri/renderD128"
#define FAR_AWAY	0x7f000000ull	/* the service never places there */

static int bad;

static void
say(int ok, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void
say(int ok, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	if (!ok) {
		printf(" - NOT as it should be");
		bad++;
	}
	printf("\n");
	fflush(stdout);
}

static int64_t
now_ns(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static int
xioctl(int fd, unsigned long req, void *arg)
{
	return ioctl(fd, req, arg) == 0 ? 0 : errno;
}

/* ---- PM4, this file's own encoding (adreno_pm4.xml) ------------------- */

static uint32_t
parity(uint32_t v)
{
	v ^= v >> 16;
	v ^= v >> 8;
	v ^= v >> 4;
	return (0x9669u >> (v & 0xf)) & 1;
}

static uint32_t
pkt7(uint32_t op, uint32_t cnt)
{
	return 0x70000000u | (cnt & 0x7fff) | parity(cnt) << 15 |
	    (op & 0x7f) << 16 | parity(op) << 23;
}

#define CP_NOP		0x10
#define CP_MEM_WRITE	0x3d

/* ---- the node ------------------------------------------------------------ */

struct bo {
	uint32_t handle;
	uint64_t iova, size;
	uint32_t *cpu;
};

static int
param(int fd, uint32_t p, uint64_t *v)
{
	struct drm_msm_param a;
	int r;

	memset(&a, 0, sizeof(a));
	a.pipe = MSM_PIPE_3D0;
	a.param = p;
	r = xioctl(fd, DRM_IOCTL_MSM_GET_PARAM, &a);
	*v = a.value;
	return r;
}

/* mmap(2) of the node; with an older libc, the driver asked directly. */
static void *
map(int fd, uint64_t off, size_t len)
{
	struct cdev_mmap cm;
	void *p;

	p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    (off_t)off);
	if (p != MAP_FAILED)
		return p;
	memset(&cm, 0, sizeof(cm));
	cm.cm_len = len;
	cm.cm_prot = PROT_READ | PROT_WRITE;
	cm.cm_flags = MAP_SHARED;
	cm.cm_offset = (off_t)off;
	if (ioctl(fd, MIOCMMAP, &cm) != 0)
		return NULL;
	return cm.cm_ret;
}

static int
bo_new(int fd, size_t size, uint32_t flags, struct bo *b)
{
	struct drm_msm_gem_new g;
	struct drm_msm_gem_info i;
	int r;

	memset(b, 0, sizeof(*b));
	memset(&g, 0, sizeof(g));
	g.size = size;
	g.flags = flags;
	if ((r = xioctl(fd, DRM_IOCTL_MSM_GEM_NEW, &g)) != 0)
		return r;
	b->handle = g.handle;
	b->size = size;
	memset(&i, 0, sizeof(i));
	i.handle = g.handle;
	i.info = MSM_INFO_GET_IOVA;
	if ((r = xioctl(fd, DRM_IOCTL_MSM_GEM_INFO, &i)) != 0)
		return r;
	b->iova = i.value;
	i.info = MSM_INFO_GET_OFFSET;
	if ((r = xioctl(fd, DRM_IOCTL_MSM_GEM_INFO, &i)) != 0)
		return r;
	if ((b->cpu = map(fd, i.value, size)) == NULL)
		return ENOMEM;
	return 0;
}

static int
submit(int fd, struct bo *cmd, uint32_t words, struct bo *tgt,
	uint32_t *fence, int *fence_fd)
{
	struct drm_msm_gem_submit_bo sb[2];
	struct drm_msm_gem_submit_cmd sc;
	struct drm_msm_gem_submit s;
	int r;

	memset(sb, 0, sizeof(sb));
	sb[0].handle = cmd->handle;
	sb[0].flags = MSM_SUBMIT_BO_READ;
	if (tgt != NULL) {
		sb[1].handle = tgt->handle;
		sb[1].flags = MSM_SUBMIT_BO_WRITE;
	}
	memset(&sc, 0, sizeof(sc));
	sc.type = MSM_SUBMIT_CMD_BUF;
	sc.submit_idx = 0;
	sc.size = words * 4;
	memset(&s, 0, sizeof(s));
	s.flags = MSM_PIPE_3D0 | (fence_fd != NULL ? MSM_SUBMIT_FENCE_FD_OUT :
	    0);
	s.nr_bos = tgt != NULL ? 2 : 1;
	s.bos = (uint64_t)(uintptr_t)sb;
	s.nr_cmds = 1;
	s.cmds = (uint64_t)(uintptr_t)&sc;
	s.fence_fd = -1;
	r = xioctl(fd, DRM_IOCTL_MSM_GEM_SUBMIT, &s);
	*fence = s.fence;
	if (fence_fd != NULL)
		*fence_fd = s.fence_fd;
	return r;
}

static int
wait_fence(int fd, uint32_t fence, int64_t ns)
{
	struct drm_msm_wait_fence w;
	int64_t t = now_ns() + ns;

	memset(&w, 0, sizeof(w));
	w.fence = fence;
	w.timeout.tv_sec = t / 1000000000LL;
	w.timeout.tv_nsec = t % 1000000000LL;
	return xioctl(fd, DRM_IOCTL_MSM_WAIT_FENCE, &w);
}

static int
sync_status(int sfd)
{
	struct sync_file_info si;

	memset(&si, 0, sizeof(si));
	if (ioctl(sfd, SYNC_IOC_FILE_INFO, &si) != 0)
		return 1000 + errno;
	return si.status;
}

static uint64_t
queue_faults(int fd)
{
	struct drm_msm_submitqueue_query q;
	uint64_t v = 0;

	memset(&q, 0, sizeof(q));
	q.id = 0;
	q.param = MSM_SUBMITQUEUE_PARAM_FAULTS;
	q.data = (uint64_t)(uintptr_t)&v;
	q.len = sizeof(v);
	if (ioctl(fd, DRM_IOCTL_MSM_SUBMITQUEUE_QUERY, &q) != 0)
		return ~0ull;
	return v;
}

/* n words from v0 at va - CP_MEM_WRITE - into cmd from word *w. */
static void
put_write(struct bo *cmd, unsigned *w, uint64_t va, uint32_t v0, unsigned n)
{
	unsigned i;

	cmd->cpu[(*w)++] = pkt7(CP_MEM_WRITE, 2 + n);
	cmd->cpu[(*w)++] = (uint32_t)va;
	cmd->cpu[(*w)++] = (uint32_t)(va >> 32);
	for (i = 0; i < n; i++)
		cmd->cpu[(*w)++] = v0 + i;
}

static int
open_node(void)
{
	int fd = open(NODE, O_RDWR);

	if (fd < 0) {
		say(0, "open %s: %s", NODE, strerror(errno));
		exit(1);
	}
	return fd;
}

/* ---- the questions --------------------------------------------------- */

static void
t_info(void)
{
	struct drm_version v;
	char name[16], date[16], desc[32];
	uint64_t id, chip, gmem, base, freq, prio, t0, t1, faults;
	int64_t n0, n1;
	int fd = open_node();

	memset(&v, 0, sizeof(v));
	memset(name, 0, sizeof(name));
	memset(date, 0, sizeof(date));
	memset(desc, 0, sizeof(desc));
	v.name = name;
	v.name_len = sizeof(name) - 1;
	v.date = date;
	v.date_len = sizeof(date) - 1;
	v.desc = desc;
	v.desc_len = sizeof(desc) - 1;
	say(xioctl(fd, DRM_IOCTL_VERSION, &v) == 0 && strcmp(name, "msm") ==
	    0 && v.version_major == 1, "version: %s %d.%d.%d, %s, \"%s\"",
	    name, v.version_major, v.version_minor, v.version_patchlevel,
	    date, desc);
	say(param(fd, MSM_PARAM_GPU_ID, &id) == 0 && id == 512 &&
	    param(fd, MSM_PARAM_CHIP_ID, &chip) == 0 && chip == 0x05010200,
	    "gpu id %llu, chip id 0x%08llx", (unsigned long long)id,
	    (unsigned long long)chip);
	say(param(fd, MSM_PARAM_GMEM_SIZE, &gmem) == 0 &&
	    param(fd, MSM_PARAM_GMEM_BASE, &base) == 0 && gmem == 0x44000,
	    "gmem 0x%llx at 0x%llx", (unsigned long long)gmem,
	    (unsigned long long)base);
	say(param(fd, MSM_PARAM_MAX_FREQ, &freq) == 0 &&
	    param(fd, MSM_PARAM_PRIORITIES, &prio) == 0 &&
	    param(fd, MSM_PARAM_FAULTS, &faults) == 0, "max freq %llu Hz, %llu "
	    "priorities, %llu faults so far", (unsigned long long)freq,
	    (unsigned long long)prio, (unsigned long long)faults);
	n0 = now_ns();
	(void)param(fd, MSM_PARAM_TIMESTAMP, &t0);
	usleep(100000);
	n1 = now_ns();
	(void)param(fd, MSM_PARAM_TIMESTAMP, &t1);
	say(t1 > t0, "timestamp: %llu ticks in %lld us (%.2f MHz)",
	    (unsigned long long)(t1 - t0), (long long)(n1 - n0) / 1000,
	    (double)(t1 - t0) * 1000.0 / (double)(n1 - n0));
	close(fd);
}

static void
t_job(void)
{
	struct bo cmd, tgt;
	struct pollfd p;
	uint32_t fence;
	unsigned w = 0, i, wrong = 0;
	int64_t t;
	int fd = open_node(), r, sfd = -1;

	say(bo_new(fd, 4096, MSM_BO_WC, &cmd) == 0 &&
	    bo_new(fd, 4096, MSM_BO_WC, &tgt) == 0, "buffers: commands at "
	    "0x%llx, target at 0x%llx", (unsigned long long)cmd.iova,
	    (unsigned long long)tgt.iova);
	if (cmd.cpu == NULL || tgt.cpu == NULL) {
		close(fd);
		return;
	}
	for (i = 0; i < 1024; i++)
		tgt.cpu[i] = 0x5a5a5a5a;
	put_write(&cmd, &w, tgt.iova + 64, 0xcafe0000, 16);
	t = now_ns();
	r = submit(fd, &cmd, w, &tgt, &fence, &sfd);
	say(r == 0, "submit: %s, fence %u, sync file %d", strerror(r), fence,
	    sfd);
	r = wait_fence(fd, fence, 1000000000LL);
	t = now_ns() - t;
	say(r == 0, "wait_fence %u: %s in %lld us", fence, strerror(r),
	    (long long)t / 1000);
	for (i = 0; i < 1024; i++)
		if (tgt.cpu[i] != (i >= 16 && i < 32 ? 0xcafe0000 + i - 16 :
		    0x5a5a5a5a))
			wrong++;
	say(wrong == 0, "the target: words 16..31 %08x..%08x, the rest "
	    "%08x; %u words wrong", tgt.cpu[16], tgt.cpu[31], tgt.cpu[0],
	    wrong);
	if (sfd >= 0) {
		p.fd = sfd;
		p.events = POLLIN;
		p.revents = 0;
		r = poll(&p, 1, 1000);
		say(r == 1 && (p.revents & POLLIN) && sync_status(sfd) == 1,
		    "sync file: poll %d (revents %x), status %d", r, p.revents,
		    sync_status(sfd));
		close(sfd);
	}
	close(fd);
}

static void
t_fault(void)
{
	struct bo cmd, tgt;
	uint64_t f0, f1, q;
	uint32_t fence;
	unsigned w = 0;
	int fd = open_node(), r, sfd = -1;

	(void)param(fd, MSM_PARAM_FAULTS, &f0);
	say(bo_new(fd, 4096, MSM_BO_WC, &cmd) == 0 &&
	    bo_new(fd, 4096, MSM_BO_WC, &tgt) == 0, "buffers");
	if (cmd.cpu == NULL || tgt.cpu == NULL) {
		close(fd);
		return;
	}
	tgt.cpu[0] = tgt.cpu[1] = 0;
	put_write(&cmd, &w, FAR_AWAY, 0xdeadbeef, 1);
	put_write(&cmd, &w, tgt.iova, 0x600d0001, 1);
	/*
	 * The bank does not stall: the access is terminated, and the GPU is
	 * expected to go on.  If it stops instead, the service's hang check
	 * ends the job and brings the GPU up again - seconds, not a second:
	 * the wait is long enough for either, and the lines say which.
	 */
	r = submit(fd, &cmd, w, &tgt, &fence, &sfd);
	r = r != 0 ? r : wait_fence(fd, fence, 15000000000LL);
	(void)param(fd, MSM_PARAM_FAULTS, &f1);
	q = queue_faults(fd);
	say(r == 0 && f1 > f0 && q == 1, "a write to 0x%llx: wait %s, faults "
	    "%llu -> %llu%s, the queue's %llu", (unsigned long long)FAR_AWAY,
	    strerror(r), (unsigned long long)f0, (unsigned long long)f1,
	    f1 > f0 + 1 ? " (the GPU was brought up again)" : "",
	    (unsigned long long)q);
	say(sync_status(sfd) < 0, "its sync file: status %d (failed)",
	    sync_status(sfd));
	printf("the write after the fault: %08x (%s)\n", tgt.cpu[0],
	    tgt.cpu[0] == 0x600d0001 ? "the GPU went on" : "not done");
	close(sfd);
	w = 0;
	put_write(&cmd, &w, tgt.iova + 4, 0x600d0002, 1);
	r = submit(fd, &cmd, w, &tgt, &fence, NULL);
	r = r != 0 ? r : wait_fence(fd, fence, 1000000000LL);
	say(r == 0 && tgt.cpu[1] == 0x600d0002 && queue_faults(fd) == q,
	    "the next job: %s, %08x, the queue's faults %llu", strerror(r),
	    tgt.cpu[1], (unsigned long long)queue_faults(fd));
	close(fd);
}

static void
t_two(void)
{
	struct bo c1, t1, c2, t2;
	uint32_t f1, f2;
	unsigned w1 = 0, w2 = 0;
	int a = open_node(), b = open_node(), r1, r2;

	say(bo_new(a, 4096, MSM_BO_WC, &t1) == 0 &&
	    bo_new(a, 4096, MSM_BO_WC, &c1) == 0 &&
	    bo_new(b, 4096, MSM_BO_WC, &t2) == 0 &&
	    bo_new(b, 4096, MSM_BO_WC, &c2) == 0 && t1.iova == t2.iova,
	    "two contexts: their targets at 0x%llx and 0x%llx",
	    (unsigned long long)t1.iova, (unsigned long long)t2.iova);
	if (c1.cpu == NULL || c2.cpu == NULL || t1.cpu == NULL ||
	    t2.cpu == NULL)
		return;
	t1.cpu[0] = t2.cpu[0] = 0;
	put_write(&c1, &w1, t1.iova, 0xaaaa0001, 1);
	put_write(&c2, &w2, t2.iova, 0xbbbb0002, 1);
	r1 = submit(a, &c1, w1, &t1, &f1, NULL);
	r2 = submit(b, &c2, w2, &t2, &f2, NULL);
	r1 = r1 != 0 ? r1 : wait_fence(a, f1, 1000000000LL);
	r2 = r2 != 0 ? r2 : wait_fence(b, f2, 1000000000LL);
	say(r1 == 0 && r2 == 0 && t1.cpu[0] == 0xaaaa0001 &&
	    t2.cpu[0] == 0xbbbb0002, "each its own: %08x, %08x (%s, %s)",
	    t1.cpu[0], t2.cpu[0], strerror(r1), strerror(r2));
	close(a);
	close(b);
}

static void
t_chain(int n)
{
	struct bo cmd, tgt;
	uint32_t fence;
	unsigned w, wrong = 0;
	int64_t t, worst = 0, sum = 0, one;
	int fd = open_node(), i, r = 0;

	if (bo_new(fd, 4096, MSM_BO_WC, &cmd) != 0 ||
	    bo_new(fd, 4096, MSM_BO_WC, &tgt) != 0) {
		say(0, "chain: no buffers");
		close(fd);
		return;
	}
	for (i = 0; i < n && r == 0; i++) {
		w = 0;
		put_write(&cmd, &w, tgt.iova + 4 * (i % 1024), (uint32_t)i, 1);
		t = now_ns();
		if ((r = submit(fd, &cmd, w, &tgt, &fence, NULL)) == 0)
			r = wait_fence(fd, fence, 1000000000LL);
		one = now_ns() - t;
		sum += one;
		if (one > worst)
			worst = one;
		if (r == 0 && tgt.cpu[i % 1024] != (uint32_t)i)
			wrong++;
	}
	say(r == 0 && wrong == 0, "chain: %d jobs, %s, %u words wrong; a job "
	    "and its wait %lld us on average, %lld at worst", i, strerror(r),
	    wrong, (long long)(i > 0 ? sum / i : 0) / 1000,
	    (long long)worst / 1000);
	close(fd);
}

int
main(int argc, char **argv)
{
	const char *what = argc > 1 ? argv[1] : "all";
	int all = strcmp(what, "all") == 0;

	if (all || strcmp(what, "info") == 0)
		t_info();
	if (all || strcmp(what, "job") == 0)
		t_job();
	if (all || strcmp(what, "fault") == 0)
		t_fault();
	if (all || strcmp(what, "two") == 0)
		t_two();
	if (all || strcmp(what, "chain") == 0)
		t_chain(argc > 2 ? atoi(argv[2]) : 100);
	printf("fdtest: %d NOT as it should be\n", bad);
	return bad != 0;
}
