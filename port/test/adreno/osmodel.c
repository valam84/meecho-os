/*
 * Модель окружения службы adreno для стенда узла рендеринга (msmtest.c):
 * то, что на машине делают VFS, VM, libchardriver и часы, — по образцу
 * модели стенда mali (port/test/mali/model.c), без её ускорителя.
 *
 *   VFS     запрос приходит с грантом на аргумент ровно того размера, что
 *           в номере; отложенные ответы и ответы select записываются;
 *           дескрипторы процессов — файлы, общие у dup/fork/SCM_RIGHTS,
 *           последнее закрытие файла устройства — закрытие его minor;
 *           cdevfd(2) — только процессу, от которого идёт вызов, и только
 *           для minor, который libchardriver видел открытым.
 *   Память  процесса-клиента: sys_vircopy только с вызывающим.
 *   VM      mmap(MAP_ANON | MAP_PREALLOC) службы — страницы модели
 *           памяти (smmumodel.c), разбросанные по физической памяти;
 *           sys_umap — адрес одной страницы; vm_remap — только из своей
 *           области целиком и по настоящему endpoint службы (не SELF), и
 *           клиент видит те же страницы.
 *   Часы    100 тиков в секунду от model_now_us(); таймеры libsys.
 *
 * Нарушение этих правил — ошибка службы, она считается (osm_errors) и
 * печатается, но сценарий идёт дальше: это не железо.
 */

#include "adreno.h"
#include "model.h"
#include "osmodel.h"

#include <minix/chardriver.h>
#include <minix/timers.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

unsigned osm_errors;
char osm_error_text[512];
endpoint_t osm_user;

void
osm_error(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(osm_error_text, sizeof(osm_error_text), fmt, ap);
	va_end(ap);
	osm_errors++;
	printf("  ОКРУЖЕНИЕ: %s\n", osm_error_text);
}

/* ---- часы, таймеры ---------------------------------------------------- */

static minix_timer_t *timers;

/* The free-running clock (read_frclock_64()): the model's microseconds. */
void
read_frclock_64(u64_t *frclk)
{
	*frclk = model_now_us();
}

u64_t
delta_frclock_64(u64_t base, u64_t cur)
{
	return cur - base;
}

clock_t
osm_ticks(void)
{
	return (clock_t)(model_now_us() / 10000);
}

void
init_timer(minix_timer_t *tp)
{
	tp->tmr_func = NULL;
	tp->tmr_next = NULL;
}

void
cancel_timer(minix_timer_t *tp)
{
	minix_timer_t **pp;

	for (pp = &timers; *pp != NULL; pp = &(*pp)->tmr_next)
		if (*pp == tp) {
			*pp = tp->tmr_next;
			break;
		}
	tp->tmr_func = NULL;
	tp->tmr_next = NULL;
}

void
set_timer(minix_timer_t *tp, clock_t ticks, tmr_func_t f, int arg)
{
	cancel_timer(tp);
	tp->tmr_exp_time = osm_ticks() + ticks;
	tp->tmr_func = f;
	tp->tmr_arg = arg;
	tp->tmr_next = timers;
	timers = tp;
}

void
expire_timers(clock_t now)
{
	minix_timer_t *tp;
	tmr_func_t f;
	int found = 1;

	while (found) {
		found = 0;
		for (tp = timers; tp != NULL; tp = tp->tmr_next)
			if (tp->tmr_exp_time <= now) {
				f = tp->tmr_func;
				cancel_timer(tp);
				f(tp->tmr_arg);
				found = 1;
				break;
			}
	}
}

int
osm_timers_pending(void)
{
	return timers != NULL;
}

int
getuptime(clock_t *ticks, clock_t *realtime, time_t *boottime)
{
	*ticks = osm_ticks();
	*realtime = *ticks;
	*boottime = 1000;
	return OK;
}

/* CLOCK_MONOTONIC as the client sees it (PM: boottime + uptime), in ns. */
int64_t
osm_mono_ns(void)
{
	return 1000LL * 1000000000LL + (int64_t)osm_ticks() * 10000000LL;
}

/* ---- VFS: гранты, ответы ------------------------------------------------ */

static struct { void *buf; size_t len; } grants[64];
static unsigned ngrants;

cp_grant_id_t
osm_grant(void *buf, size_t len)
{
	unsigned i = ngrants++ % 64;

	grants[i].buf = buf;
	grants[i].len = len;
	return (cp_grant_id_t)(1000 + i);
}

static void *
grant_at(endpoint_t ep, cp_grant_id_t g, vir_bytes off, size_t len)
{
	unsigned i = (unsigned)(g - 1000);

	if (ep != VFS_PROC_NR || g < 1000 || i >= 64 ||
	    off + len > grants[i].len)
		return NULL;
	return (char *)grants[i].buf + off;
}

int
sys_safecopyfrom(endpoint_t src, cp_grant_id_t g, vir_bytes off,
	vir_bytes addr, size_t len)
{
	void *p = grant_at(src, g, off, len);

	if (p == NULL)
		return EPERM;
	memcpy((void *)addr, p, len);
	return OK;
}

int
sys_safecopyto(endpoint_t dst, cp_grant_id_t g, vir_bytes off,
	vir_bytes addr, size_t len)
{
	void *p = grant_at(dst, g, off, len);

	if (p == NULL)
		return EPERM;
	memcpy(p, (void *)addr, len);
	return OK;
}

int
sys_vircopy(endpoint_t sp, vir_bytes sv, endpoint_t dp, vir_bytes dv,
	phys_bytes n, int flags)
{
	endpoint_t other = sp == SELF ? dp : sp;

	(void)flags;
	if (other != osm_user) {
		osm_error("sys_vircopy with %d, not the caller %d", other,
		    osm_user);
		return EPERM;
	}
	if ((sp != SELF && sv < 4096) || (dp != SELF && dv < 4096))
		return EFAULT;
	memcpy((void *)dv, (void *)sv, n);
	return OK;
}

static struct { cdev_id_t id; int r; } replies[1024];
static unsigned nreplies;

void
chardriver_reply_task(endpoint_t endpt, cdev_id_t id, int r)
{
	if (endpt != VFS_PROC_NR)
		osm_error("a reply to %d, not VFS", endpt);
	if (nreplies < 1024) {
		replies[nreplies].id = id;
		replies[nreplies].r = r;
		nreplies++;
	}
}

int
osm_reply(cdev_id_t id, int *r)
{
	unsigned i;

	for (i = 0; i < nreplies; i++)
		if (replies[i].id == id) {
			*r = replies[i].r;
			return 1;
		}
	return 0;
}

/* ---- VM ------------------------------------------------------------------ */

endpoint_t
sef_self(void)
{
	return OSM_SELF_EP;
}

void *
model_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *p;

	(void)addr;
	(void)prot;
	(void)off;
	if (fd != -1 || !(flags & MAP_ANON) || !(flags & MAP_PREALLOC))
		osm_error("mmap of the service's: flags 0x%x fd %d - not "
		    "anonymous memory, every page present", flags, fd);
	if ((p = model_pages_new(len)) == NULL)
		return MAP_FAILED;
	return p;
}

int
model_munmap(void *addr, size_t len)
{
	return model_pages_free(addr, len);
}

int
sys_umap(endpoint_t ep, int seg, vir_bytes vir, vir_bytes len,
	phys_bytes *phys)
{
	uint64_t pa;

	if (ep != SELF || seg != VM_D || len == 0 || len > 4096 ||
	    ((vir & 4095) + len) > 4096) {
		osm_error("sys_umap(%d, %d, 0x%lx, %lu): one page of the "
		    "service's own, wanted", ep, seg, (unsigned long)vir,
		    (unsigned long)len);
		return EFAULT;
	}
	if (model_umap((const void *)vir, len, &pa) != 0)
		return EFAULT;
	*phys = (phys_bytes)pa;
	return OK;
}

struct osm_remap osm_remaps[256];
unsigned osm_nremaps;

static void *
remap(endpoint_t d, endpoint_t s, void *da, void *sa, size_t size, int ro)
{
	/*
	 * VM: both ends by their endpoint (SELF is none to vm_isokendpt() -
	 * on the board the call failed without a word), the source the start
	 * of a region, and the region whole.
	 */
	if (s != OSM_SELF_EP || !model_pages_whole(sa, size))
		return MAP_FAILED;
	if (osm_nremaps < 256) {
		osm_remaps[osm_nremaps].who = d;
		osm_remaps[osm_nremaps].src = sa;
		osm_remaps[osm_nremaps].addr = da;
		osm_remaps[osm_nremaps].len = size;
		osm_remaps[osm_nremaps].ro = ro;
		osm_nremaps++;
	}
	return sa;	/* the process sees the very pages the service has */
}

void *
model_vm_remap(endpoint_t d, endpoint_t s, void *da, void *sa, size_t size)
{
	return remap(d, s, da, sa, size, 0);
}

void *
model_vm_remap_ro(endpoint_t d, endpoint_t s, void *da, void *sa,
	size_t size)
{
	return remap(d, s, da, sa, size, 1);
}

/* ---- дескрипторы: cdevfd(2) VFS, открытые minor libchardriver ------------ */

#define MFILES		1024
#define MPROCS		8
#define MFDS		256

static struct mfile {
	unsigned count;			/* descriptors of it; 0: free */
	int minor;			/* -1: another device's (a pipe) */
	int flags;
	uint64_t size;
} mfiles[MFILES];

static struct {
	endpoint_t who;
	int fd[MFDS];			/* the file + 1, or 0 */
} mprocs[MPROCS];

static int open_minors[512];
static unsigned nopen_minors;
static unsigned sel_reply[4096];
int (*osm_cdev_close)(devminor_t minor);

static int
mproc(endpoint_t who)
{
	int i;

	for (i = 0; i < MPROCS; i++)
		if (mprocs[i].who == who)
			return i;
	for (i = 0; i < MPROCS; i++)
		if (mprocs[i].who == 0) {
			mprocs[i].who = who;
			return i;
		}
	osm_error("too many processes in the model");
	return 0;
}

static int
mfd_install(endpoint_t who, int file)
{
	int p = mproc(who), fd;

	for (fd = 3; fd < MFDS; fd++)
		if (mprocs[p].fd[fd] == 0) {
			mprocs[p].fd[fd] = file + 1;
			mfiles[file].count++;
			return fd;
		}
	return -1;
}

static int
mfd_file(endpoint_t who, int fd)
{
	int p = mproc(who);

	if (fd < 0 || fd >= MFDS || mprocs[p].fd[fd] == 0)
		return -1;
	return mprocs[p].fd[fd] - 1;
}

int
chardriver_open_minor(devminor_t minor)
{
	unsigned i;

	for (i = 0; i < nopen_minors; i++)
		if (open_minors[i] == minor)
			return OK;
	if (nopen_minors >= 256 - 17)
		return -ENFILE;
	open_minors[nopen_minors++] = minor;
	return OK;
}

static int
minor_seen(devminor_t minor)
{
	unsigned i;

	for (i = 0; i < nopen_minors; i++)
		if (open_minors[i] == minor)
			return 1;
	return 0;
}

int
cdevfd_new(endpoint_t endpt, devminor_t minor, int flags, uint64_t size)
{
	int f, fd;

	if (endpt != osm_user) {
		osm_error("cdevfd_new for %d, which does not wait on the "
		    "driver (%d does)", endpt, osm_user);
		return -EPERM;
	}
	if (!minor_seen(minor))
		osm_error("cdevfd_new of minor %d that libchardriver never saw "
		    "opened: its requests would be dropped", minor);
	if (flags & ~(O_ACCMODE | O_CLOEXEC))
		return -EINVAL;
	for (f = 0; f < MFILES && mfiles[f].count != 0; f++)
		;
	if (f == MFILES)
		return -ENFILE;
	mfiles[f].minor = minor;
	mfiles[f].flags = flags;
	mfiles[f].size = size;
	if ((fd = mfd_install(endpt, f)) < 0)
		return -EMFILE;
	return fd;
}

int
cdevfd_get(endpoint_t endpt, int fd)
{
	int f;

	if (endpt != osm_user) {
		osm_error("cdevfd_get for %d, which does not wait on the "
		    "driver (%d does)", endpt, osm_user);
		return -EPERM;
	}
	if ((f = mfd_file(endpt, fd)) < 0)
		return -EBADF;
	if (mfiles[f].minor < 0)
		return -EINVAL;
	return mfiles[f].minor;
}

/* The same file for process to (dup, fork, SCM_RIGHTS): a new descriptor. */
int
osm_fd_pass(endpoint_t from, int fd, endpoint_t to)
{
	int f = mfd_file(from, fd);

	if (f < 0) {
		osm_error("pass of a closed descriptor %d of %d", fd, from);
		return -1;
	}
	return mfd_install(to, f);
}

/* close(2): the last descriptor of a file of the service's closes its minor. */
int
osm_fd_close(endpoint_t who, int fd)
{
	int p = mproc(who), f = mfd_file(who, fd);

	if (f < 0)
		return EBADF;
	mprocs[p].fd[fd] = 0;
	if (--mfiles[f].count == 0 && mfiles[f].minor >= 0) {
		if (osm_cdev_close == NULL)
			osm_error("no device to close minor %d on",
			    mfiles[f].minor);
		else
			(void)osm_cdev_close(mfiles[f].minor);
	}
	return OK;
}

/* A process goes: every descriptor of it closes. */
void
osm_proc_exit(endpoint_t who)
{
	int p = mproc(who), fd;

	for (fd = 0; fd < MFDS; fd++)
		if (mprocs[p].fd[fd] != 0)
			(void)osm_fd_close(who, fd);
	mprocs[p].who = 0;
}

/* A descriptor of a minor of the service's, as open(2) made it. */
int
osm_fd_device(endpoint_t who, devminor_t minor)
{
	int f;

	for (f = 0; f < MFILES && mfiles[f].count != 0; f++)
		;
	mfiles[f].minor = minor;
	mfiles[f].flags = O_RDWR;
	mfiles[f].size = 0;
	return mfd_install(who, f);
}

int
osm_fd_minor(endpoint_t who, int fd)
{
	int f = mfd_file(who, fd);

	return f < 0 ? -1 : mfiles[f].minor;
}

unsigned
osm_files_open(void)
{
	unsigned f, n = 0;

	for (f = 0; f < MFILES; f++)
		if (mfiles[f].count != 0 && mfiles[f].minor >= 0)
			n++;
	return n;
}

void
chardriver_reply_select(endpoint_t endpt, devminor_t minor, int ops)
{
	if (endpt != VFS_PROC_NR)
		osm_error("a select reply to %d, not VFS", endpt);
	if (minor < 0 || minor >= 4096)
		osm_error("a select reply for minor %d", minor);
	else
		sel_reply[minor] |= (unsigned)ops;
}

unsigned
osm_select_reply(devminor_t minor)
{
	unsigned r = sel_reply[minor];

	sel_reply[minor] = 0;
	return r;
}

void
osm_reset(void)
{
	memset(mfiles, 0, sizeof(mfiles));
	memset(mprocs, 0, sizeof(mprocs));
	memset(sel_reply, 0, sizeof(sel_reply));
	nopen_minors = 0;
	nreplies = 0;
	osm_nremaps = 0;
	osm_cdev_close = NULL;
	timers = NULL;
}
