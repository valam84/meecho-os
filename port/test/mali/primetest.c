/*
 * Стенд дескрипторов службы mali: PRIME (буфер - дескриптор dma-buf) и
 * sync file (забор - дескриптор), порядок заданий разных контекстов,
 * без железа.  Веха 11.6, вторая часть.
 *
 * Собирает настоящие mali_drm.c, mali_mmu.c, mali_bo.c и mali_sched.c на
 * модели (model.c).  Таблицы дескрипторов процессов держит модель VFS:
 * дескриптор служба получает и называет только через cdevfd(2) и только
 * для процесса, от которого вызов; передача через сокет и fork - тот же
 * файл у другого процесса (model_fd_pass()); последнее закрытие файла -
 * закрытие minor (cdr_close()).  Модель libchardriver требует, чтобы minor
 * дескриптора был объявлен открытым до того, как VFS его сделает (иначе
 * на машине его запросы пропадали бы без ответа).
 *
 * Что проверяется - кроме ответов на каждый запрос:
 *
 *   - один буфер: импорт в другой контекст - те же страницы на своём
 *     адресе, запись ускорителя одного контекста видна процессору и
 *     ускорителю другого; один дескриптор на буфер, пока он открыт;
 *   - жизнь: буфер жив, пока жив хоть кто-то - номер, дескриптор,
 *     размещение; последний ушёл - страницы вернулись, minor свободен;
 *   - порядок: задание контекста, которое называет общий буфер, ждёт
 *     задания другого контекста на нём (неявный забор), задание с чужим
 *     забором (sync file) стоит в службе, пока забор не пройден, и все
 *     задания его контекста - за ним;
 *   - забор как дескриптор: poll (select с уведомлением), слияние,
 *     сведения, упавшее задание; объект синхронизации целиком - общий;
 *   - права: импорт только дескриптора, который у вызывающего есть;
 *     служба называет VFS только того, кто спрашивает;
 *   - пределы: дескрипторов-объектов MAX_OBJ, и minor-ы переиспользуются,
 *     так что libchardriver не переполняется.
 *
 *	sh run.sh       (собирает и этот стенд)
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

#include <minix/cachectl.h>

#include "model.h"
#include "drm.h"
#include "panfrost_drm.h"
#include <sys/ioc_mmap.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>

extern struct chardriver mali_chardriver;

#define RENDER		128
#define OBJ_MINOR	256
#define USER_A		0x100a
#define USER_C		0x100c
#define VALUE(c, i)	((uint32_t)(c) << 24 | (uint32_t)(i))

static cdev_id_t next_id = 1;
static cdev_id_t last_id;

/*
 * Запрос, как его передал бы VFS.  Ошибки VFS (cdevfd(2)) у службы - те,
 * что VFS дал, а на машине все коды ошибок отрицательны; у стенда коды
 * хоста положительны, и ответ сравнивается по модулю.
 */
static int
io(int minor, unsigned long req, void *arg, endpoint_t user)
{
	cp_grant_id_t g = model_grant(arg, IOCPARM_LEN(req));
	int r;

	last_id = next_id++;
	model_user = user;
	if (minor >= OBJ_MINOR && !model_minor_seen(minor))
		return 9999;	/* libchardriver: no reply at all */
	r = mali_chardriver.cdr_ioctl(minor, req, VFS_EP, g, 0, user,
	    last_id);
	return r < 0 ? -r : r;
}

static int
open_ctx(endpoint_t user)
{
	int r = mali_chardriver.cdr_open(RENDER, 0, user);

	CHECK(r >= 0 && (r & CDEV_CLONED), "open: %d", r);
	return r & ~CDEV_CLONED;
}

static void
start(void)
{
	model_reset();
	model_fds_reset();
	model_cdev_close = mali_chardriver.cdr_close;
	mali_sched_init();
	mali_bo_limits(768u << 20, 512u << 20);
	mali_drm_init();
}

/* CLOCK_MONOTONIC, как его видит клиент (PM: boottime + uptime). */
static int64_t
mono(void)
{
	return 1000LL * 1000000000LL + (int64_t)model_now() * 10000000LL;
}

struct bo {
	uint32_t handle;
	uint64_t va;
	char *cpu;
	size_t size;
};

static int
create(int m, endpoint_t u, uint32_t size, uint32_t flags, struct bo *b)
{
	struct drm_panfrost_create_bo c;
	int r;

	memset(&c, 0, sizeof(c));
	c.size = size;
	c.flags = flags;
	if ((r = io(m, DRM_IOCTL_PANFROST_CREATE_BO, &c, u)) != OK)
		return r;
	b->handle = c.handle;
	b->va = c.offset;
	b->size = size;
	b->cpu = NULL;
	return OK;
}

static uint64_t
offset_of(int m, endpoint_t u, uint32_t h)
{
	struct drm_panfrost_get_bo_offset o;

	memset(&o, 0, sizeof(o));
	o.handle = h;
	if (io(m, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &o, u) != OK)
		return 0;
	return o.offset;
}

static int
map(int m, endpoint_t u, struct bo *b)
{
	struct drm_panfrost_mmap_bo mb;
	struct cdev_mmap cm;
	int r;

	memset(&mb, 0, sizeof(mb));
	mb.handle = b->handle;
	if ((r = io(m, DRM_IOCTL_PANFROST_MMAP_BO, &mb, u)) != OK)
		return r;
	memset(&cm, 0, sizeof(cm));
	cm.cm_len = b->size;
	cm.cm_prot = PROT_READ | PROT_WRITE;
	cm.cm_flags = MAP_SHARED;
	cm.cm_offset = (off_t)mb.offset;
	if ((r = io(m, MIOCMMAP, &cm, u)) != OK)
		return r;
	b->cpu = cm.cm_ret;
	return OK;
}

/* Импорт в контекст m: номер, адрес, отображение. */
static int
import_bo(int m, endpoint_t u, int fd, size_t size, struct bo *b)
{
	struct drm_prime_handle p;
	int r;

	memset(&p, 0, sizeof(p));
	p.fd = fd;
	if ((r = io(m, DRM_IOCTL_PRIME_FD_TO_HANDLE, &p, u)) != OK)
		return r;
	b->handle = p.handle;
	b->va = offset_of(m, u, p.handle);
	b->size = size;
	b->cpu = NULL;
	return OK;
}

static int
export_bo(int m, endpoint_t u, uint32_t h, uint32_t flags, int *fd)
{
	struct drm_prime_handle p;
	int r;

	memset(&p, 0, sizeof(p));
	p.handle = h;
	p.flags = flags;
	p.fd = -1;
	r = io(m, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p, u);
	*fd = r == OK ? p.fd : -1;
	return r;
}

static uint32_t
syncobj(int m, endpoint_t u, int signaled)
{
	struct drm_syncobj_create c;

	memset(&c, 0, sizeof(c));
	c.flags = signaled ? DRM_SYNCOBJ_CREATE_SIGNALED : 0;
	CHECK(io(m, DRM_IOCTL_SYNCOBJ_CREATE, &c, u) == OK && c.handle != 0,
	    "syncobj create");
	return c.handle;
}

static int
so_export(int m, endpoint_t u, uint32_t so, uint32_t flags, int *fd)
{
	struct drm_syncobj_handle a;
	int r;

	memset(&a, 0, sizeof(a));
	a.handle = so;
	a.flags = flags;
	a.fd = -1;
	r = io(m, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &a, u);
	*fd = r == OK ? a.fd : -1;
	return r;
}

static int
so_import(int m, endpoint_t u, int fd, uint32_t flags, uint32_t *so)
{
	struct drm_syncobj_handle a;
	int r;

	memset(&a, 0, sizeof(a));
	a.handle = *so;
	a.flags = flags;
	a.fd = fd;
	r = io(m, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &a, u);
	*so = a.handle;
	return r;
}

static int
info(int fd, endpoint_t u, int *status, unsigned *n)
{
	struct sync_file_info i;
	int r;

	memset(&i, 0, sizeof(i));
	r = io(model_fd_minor(u, fd), SYNC_IOC_FILE_INFO, &i, u);
	*status = i.status;
	*n = i.num_fences;
	return r;
}

static int
merge(int fd1, int fd2, endpoint_t u, int *out)
{
	struct sync_merge_data m;
	int r;

	memset(&m, 0, sizeof(m));
	m.fd2 = fd2;
	m.fence = -1;
	r = io(model_fd_minor(u, fd1), SYNC_IOC_MERGE, &m, u);
	*out = r == OK ? m.fence : -1;
	return r;
}

/* select(2) на дескрипторе: готов сейчас (ops), или ждёт уведомления. */
static unsigned
sel(int fd, endpoint_t u, unsigned ops)
{
	return (unsigned)mali_chardriver.cdr_select(model_fd_minor(u, fd),
	    ops | CDEV_NOTIFY, VFS_EP);
}

/* Цепочка из n WRITE_VALUE в code: задание i пишет tag<<24|i в va+0x1000+4i. */
static void
chain_to(struct bo *code, uint64_t va, unsigned n, unsigned tag)
{
	unsigned i;
	char *d;

	for (i = 0; i < n; i++) {
		d = code->cpu + 64 * i;
		memset(d, 0, 64);
		*(uint32_t *)(d + 16) = 1 | (2 << 1) | ((i + 1) << 16);
		*(uint64_t *)(d + 24) = i + 1 < n ? code->va + 64 * (i + 1) : 0;
		*(uint64_t *)(d + 32) = va + 0x1000 + 4 * i;
		*(uint32_t *)(d + 40) = 6;
		*(uint64_t *)(d + 48) = VALUE(tag, i);
	}
}

static unsigned
wrong_at(const char *cpu, unsigned n, unsigned tag)
{
	unsigned i, w = 0;

	for (i = 0; i < n; i++)
		w += ((const uint32_t *)(cpu + 0x1000))[i] != VALUE(tag, i);
	return w;
}

static int
submit(int m, endpoint_t u, uint64_t jc, uint32_t *bos, unsigned nbos,
	uint32_t *in, unsigned nin, uint32_t out, uint32_t req)
{
	struct drm_panfrost_submit s;

	memset(&s, 0, sizeof(s));
	s.jc = jc;
	s.bo_handles = (uint64_t)(uintptr_t)bos;
	s.bo_handle_count = nbos;
	s.in_syncs = (uint64_t)(uintptr_t)in;
	s.in_sync_count = nin;
	s.out_sync = out;
	s.requirements = req;
	return io(m, DRM_IOCTL_PANFROST_SUBMIT, &s, u);
}

static int
wait_bo(int m, endpoint_t u, uint32_t h, int64_t deadline)
{
	struct drm_panfrost_wait_bo w;

	memset(&w, 0, sizeof(w));
	w.handle = h;
	w.timeout_ns = deadline;
	return io(m, DRM_IOCTL_PANFROST_WAIT_BO, &w, u);
}

static int
wait_sync(int m, endpoint_t u, uint32_t *h, unsigned n, uint32_t flags,
	int64_t deadline)
{
	struct drm_syncobj_wait sw;

	memset(&sw, 0, sizeof(sw));
	sw.handles = (uint64_t)(uintptr_t)h;
	sw.count_handles = n;
	sw.flags = flags;
	sw.timeout_nsec = deadline;
	return io(m, DRM_IOCTL_SYNCOBJ_WAIT, &sw, u);
}

static int
sync_array(int m, endpoint_t u, unsigned long req, uint32_t h)
{
	struct drm_syncobj_array a;

	memset(&a, 0, sizeof(a));
	a.handles = (uint64_t)(uintptr_t)&h;
	a.count_handles = 1;
	return io(m, req, &a, u);
}

static int
gem_close(int m, endpoint_t u, uint32_t h)
{
	struct drm_gem_close gc;

	memset(&gc, 0, sizeof(gc));
	gc.handle = h;
	return io(m, DRM_IOCTL_GEM_CLOSE, &gc, u);
}

/* Задание в слоте s: исполнить и завершить линией. */
static int
run(int s, int n)
{
	return model_run(s) == n && model_line(MALI_IRQ_JOB) == 1;
}

/* Всё закрыто: ни файлов, ни страниц, ни объектов. */
static void
all_gone(const char *what)
{
	CHECK(model_files_open() == 0, "%s: %u file(s) of the device open",
	    what, model_files_open());
	CHECK(model_live_pages() == 0 && model_allocs_live() == 0 &&
	    mali_bo_total() == 0, "%s: left %u page(s), %u allocation(s), %zu "
	    "bytes", what, model_live_pages(), model_allocs_live(),
	    mali_bo_total());
	CHECK(model_errors == 0, "%s: model: %s", what, model_error_text);
}

/* ---- PRIME ---------------------------------------------------------------- */

static void
test_prime(void)
{
	struct drm_get_cap cap;
	struct bo a, l, hp, bb, code, imp, imp2, back, child;
	struct cdev_mmap cm;
	uint32_t hs[2], h, so;
	int ca, cb, cc, cz, d1, d2, d3, pipe_fd, node_fd, cfd, r, i, m1;
	char *md;

	printf("PRIME: буфер дескриптором, импорт в другой контекст и процесс\n");
	start();
	ca = open_ctx(USER_A);
	cb = open_ctx(USER_A);

	memset(&cap, 0, sizeof(cap));
	cap.capability = DRM_CAP_PRIME;
	CHECK(io(ca, DRM_IOCTL_GET_CAP, &cap, USER_A) == OK && cap.value == 3,
	    "cap prime %llu", (unsigned long long)cap.value);

	CHECK(create(ca, USER_A, 0x4000, 0, &a) == OK && map(ca, USER_A, &a) ==
	    OK, "a");
	CHECK(create(ca, USER_A, 0x400000, 0, &l) == OK, "l");
	CHECK(create(ca, USER_A, 0x200000, PANFROST_BO_NOEXEC |
	    PANFROST_BO_HEAP, &hp) == OK, "heap");
	CHECK(create(cb, USER_A, 0x100000, PANFROST_BO_NOEXEC, &bb) == OK,
	    "b's own");
	CHECK(create(cb, USER_A, 0x4000, 0, &code) == OK && map(cb, USER_A,
	    &code) == OK, "b's code");
	for (i = 0; i < 256; i++)
		a.cpu[0x2000 + i] = (char)i;

	/* Вывоз: дескриптор, его minor, размер, флаги. */
	r = export_bo(ca, USER_A, a.handle, DRM_CLOEXEC | DRM_RDWR, &d1);
	m1 = model_fd_minor(USER_A, d1);
	CHECK(r == OK && d1 >= 0 && m1 >= OBJ_MINOR, "export: %d, fd %d, "
	    "minor %d", r, d1, m1);
	CHECK(model_fd_size(USER_A, d1) == 0x4000, "lseek to the end: %lld",
	    (long long)model_fd_size(USER_A, d1));
	CHECK(model_fd_flags(USER_A, d1) == (O_CLOEXEC | O_RDWR), "flags 0x%x",
	    model_fd_flags(USER_A, d1));
	r = export_bo(ca, USER_A, a.handle, 0, &d2);
	CHECK(r == OK && model_fd_minor(USER_A, d2) == m1 &&
	    model_fd_flags(USER_A, d2) == O_RDONLY, "a second export: the same "
	    "minor, read-only (%d, %d)", model_fd_minor(USER_A, d2),
	    model_fd_flags(USER_A, d2));
	CHECK(export_bo(ca, USER_A, 99, DRM_CLOEXEC, &d3) == ENOENT, "export "
	    "of handle 99");
	CHECK(export_bo(ca, USER_A, a.handle, 0x1234, &d3) == EINVAL,
	    "export with flags 0x1234");
	CHECK(export_bo(ca, USER_A, hp.handle, 0, &d3) == EINVAL, "a heap "
	    "exported");

	/* Ввоз: тот же контекст - тот же номер; другой - новый, свой адрес. */
	CHECK(import_bo(ca, USER_A, d1, 0x4000, &imp) == OK && imp.handle ==
	    a.handle, "import into the exporter: handle %u", imp.handle);
	CHECK(import_bo(cb, USER_A, d1, 0x4000, &imp) == OK && imp.handle == 3 &&
	    imp.va != 0 && imp.va != bb.va && imp.va != code.va, "import into "
	    "b: handle %u at 0x%llx", imp.handle, (unsigned long long)imp.va);
	CHECK(imp.va == 0x2104000, "b's address for it 0x%llx: code, aligned "
	    "to its size, after b's own", (unsigned long long)imp.va);
	CHECK(import_bo(cb, USER_A, d2, 0x4000, &imp2) == OK && imp2.handle ==
	    imp.handle, "the second export into b: handle %u", imp2.handle);
	CHECK(import_bo(cb, USER_A, -1, 0x4000, &imp2) == EBADF, "import -1");
	pipe_fd = model_fd_foreign(USER_A);
	CHECK(import_bo(cb, USER_A, pipe_fd, 0x4000, &imp2) == EINVAL,
	    "import a pipe");
	/* A render node's descriptor: its last close closes its context. */
	cz = open_ctx(USER_A);
	node_fd = model_fd_device(USER_A, cz);
	CHECK(import_bo(cb, USER_A, node_fd, 0x4000, &imp2) == EINVAL,
	    "import the render node");

	/* Одни страницы: отображение импорта и mmap(2) дескриптора. */
	CHECK(map(cb, USER_A, &imp) == OK && memcmp(imp.cpu + 0x2000,
	    a.cpu + 0x2000, 256) == 0, "b's mapping of the import");
	memset(&cm, 0, sizeof(cm));
	cm.cm_len = 0x4000;
	cm.cm_prot = PROT_READ | PROT_WRITE;
	cm.cm_flags = MAP_SHARED;
	CHECK(io(m1, MIOCMMAP, &cm, USER_A) == OK, "mmap of the descriptor");
	md = cm.cm_ret;
	md[0x2100] = 0x5a;
	CHECK(a.cpu[0x2100] == 0x5a && memcmp(md + 0x2000, a.cpu + 0x2000,
	    256) == 0, "the descriptor's mapping is the buffer");
	cm.cm_offset = 0x1000;
	cm.cm_len = 0x3000;
	CHECK(io(m1, MIOCMMAP, &cm, USER_A) == EINVAL, "a part mapped");
	cm.cm_offset = 0;
	cm.cm_len = 0x4000;
	cm.cm_prot = PROT_READ | PROT_EXEC;
	CHECK(io(m1, MIOCMMAP, &cm, USER_A) == EINVAL, "mapped executable");

	/* Ускоритель b пишет в импорт, ускоритель a - в свой буфер. */
	memset(a.cpu + 0x1000, 0, 0x100);
	chain_to(&code, imp.va, 64, 0xb1);
	hs[0] = code.handle;
	hs[1] = imp.handle;
	CHECK(submit(cb, USER_A, code.va, hs, 2, NULL, 0, 0, 0) == OK &&
	    run(1, 64), "b's chain: %s", why);
	CHECK(wrong_at(a.cpu, 64, 0xb1) == 0, "a sees %u wrong word(s)",
	    wrong_at(a.cpu, 64, 0xb1));
	chain_to(&a, a.va, 64, 0xa1);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, 0, 0) == OK &&
	    run(1, 64), "a's chain: %s", why);
	CHECK(wrong_at(imp.cpu, 64, 0xa1) == 0, "b sees %u wrong word(s)",
	    wrong_at(imp.cpu, 64, 0xa1));

	/*
	 * Неявный порядок: задание b на общем буфере в слоте 1 не кончилось -
	 * задание a на нём же (фрагментное, слот 0 свободен) стоит в службе,
	 * WAIT_BO a видит буфер занятым; b кончилось - a идёт.
	 */
	chain_to(&code, imp.va, 4, 0xb2);
	CHECK(submit(cb, USER_A, code.va, hs, 2, NULL, 0, 0, 0) == OK &&
	    model_slot_pending(1), "b's job on the shared buffer");
	chain_to(&a, a.va, 4, 0xa2);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, 0,
	    PANFROST_JD_REQ_FS) == OK && !model_slot_pending(0), "a's job on "
	    "it went to slot 0 while b's ran");
	CHECK(wait_bo(ca, USER_A, a.handle, 0) == EBUSY, "a's wait_bo: idle "
	    "while b's job runs");
	CHECK(run(1, 4) && model_slot_pending(0), "b's done, a's in slot 0");
	CHECK(run(0, 4) && wait_bo(ca, USER_A, a.handle, 0) == OK, "a's done");

	/* Жизнь: a закрывает номер и дескрипторы - буфер жив у b. */
	CHECK(gem_close(ca, USER_A, a.handle) == OK, "a's handle closed");
	CHECK(model_fd_close(USER_A, d1) == OK && model_fd_close(USER_A, d2) ==
	    OK && model_fd_close(USER_A, node_fd) == OK, "descriptors closed");
	CHECK(model_files_open() == 0 && memcmp(imp.cpu + 0x2000, md + 0x2000,
	    256) == 0, "b's buffer lives on (%u files)", model_files_open());
	CHECK(export_bo(cb, USER_A, imp.handle, DRM_CLOEXEC | DRM_RDWR, &d3) ==
	    OK && model_fd_minor(USER_A, d3) == m1, "b exports its import: "
	    "minor %d again", model_fd_minor(USER_A, d3));
	CHECK(import_bo(ca, USER_A, d3, 0x4000, &back) == OK && back.handle ==
	    a.handle && back.va == a.va, "a imports it back: handle %u at "
	    "0x%llx", back.handle, (unsigned long long)back.va);

	/* Другой процесс: дескриптор через сокет; свой контекст, свой адрес. */
	cc = open_ctx(USER_C);
	for (i = 0; i < 4; i++)
		(void)model_fd_foreign(USER_C);	/* a number a does not have */
	cfd = model_fd_pass(USER_A, d3, USER_C);
	CHECK(import_bo(cc, USER_A, cfd, 0x4000, &child) == EBADF, "a imports "
	    "c's descriptor number: not a's");
	CHECK(import_bo(cc, USER_C, cfd, 0x4000, &child) == OK && child.handle ==
	    1 && child.va == 0x2000000, "c imports: handle %u at 0x%llx",
	    child.handle, (unsigned long long)child.va);
	CHECK(model_fd_size(USER_C, cfd) == 0x4000, "c's lseek: 0x%llx",
	    (unsigned long long)model_fd_size(USER_C, cfd));
	CHECK(map(cc, USER_C, &child) == OK && memcmp(child.cpu + 0x2000,
	    imp.cpu + 0x2000, 256) == 0, "c's mapping");
	model_proc_exit(USER_C);
	CHECK(mali_chardriver.cdr_close(cc) == OK, "c's context closed");

	/* Всё прочь: страницы возвращаются с последним. */
	CHECK(model_fd_close(USER_A, d3) == OK && model_fd_close(USER_A,
	    pipe_fd) == OK, "the last descriptors");
	CHECK(mali_chardriver.cdr_close(ca) == OK && mali_chardriver.cdr_close(
	    cb) == OK, "the contexts closed");
	(void)l;
	(void)so;
	(void)h;
	all_gone("prime");
}

/* ---- sync file ------------------------------------------------------------ */

static void
test_syncfile(void)
{
	struct bo a, b, ab;
	uint32_t so, sin, sout, snap, sig, x, y, z, h99, hs[2];
	int ca, cb, sf, s2, fsig, frun, frun2, fm, fo, d, pipe_fd, st, r, i;
	unsigned n;

	printf("sync file: забор дескриптором, задание после чужого задания\n");
	start();
	ca = open_ctx(USER_A);
	cb = open_ctx(USER_A);
	CHECK(create(ca, USER_A, 0x4000, 0, &a) == OK && map(ca, USER_A, &a) ==
	    OK, "a");
	CHECK(create(cb, USER_A, 0x4000, 0, &b) == OK && map(cb, USER_A, &b) ==
	    OK, "b");

	so = syncobj(ca, USER_A, 0);
	CHECK(so_export(ca, USER_A, so, 1, &sf) == EINVAL, "a sync object "
	    "without a fence");
	CHECK(so_export(ca, USER_A, 99, 1, &sf) == ENOENT, "sync object 99");
	CHECK(so_export(ca, USER_A, so, 2, &sf) == EINVAL, "flags 2");

	/* Задание a; его забор - дескриптор, ещё не готов. */
	chain_to(&a, a.va, 8, 0x11);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, so, 0) == OK,
	    "a's job");
	CHECK(so_export(ca, USER_A, so, 1, &sf) == OK && sf >= 0, "its sync "
	    "file");
	CHECK(info(sf, USER_A, &st, &n) == OK && st == 0 && n == 1, "file "
	    "info: status %d, %u fence(s)", st, n);
	CHECK(sel(sf, USER_A, CDEV_OP_RD) == 0, "select: ready before the job "
	    "is over");

	/* b ввозит его и ставит задание после: оно стоит в службе. */
	sin = syncobj(cb, USER_A, 0);
	CHECK(so_import(cb, USER_A, sf, 1, &sin) == OK, "b imports it");
	sout = syncobj(cb, USER_A, 0);
	chain_to(&b, b.va, 8, 0x22);
	CHECK(submit(cb, USER_A, b.va, &b.handle, 1, &sin, 1, sout,
	    PANFROST_JD_REQ_FS) == OK && !model_slot_pending(0), "b's job ran "
	    "before a's fence");
	CHECK(wait_sync(cb, USER_A, &sout, 1, 0, mono()) == ETIME, "b's sync "
	    "object over at once");
	/* Ещё одно задание b, без чужих заборов: за первым. */
	chain_to(&b, b.va, 8, 0x23);
	CHECK(submit(cb, USER_A, b.va, &b.handle, 1, NULL, 0, 0,
	    PANFROST_JD_REQ_FS) == OK && !model_slot_pending(0), "b's second "
	    "job overtook its first");

	CHECK(run(1, 8), "a's job: %s", why);
	CHECK(model_select_reply(model_fd_minor(USER_A, sf)) == CDEV_OP_RD,
	    "the select was not told");
	CHECK(info(sf, USER_A, &st, &n) == OK && st == 1, "status %d", st);
	CHECK(model_slot_pending(0), "b's job did not go after a's");
	CHECK(run(0, 8) && model_slot_pending(0) && run(0, 8), "b's two jobs");
	CHECK(wait_sync(cb, USER_A, &sout, 1, 0, mono()) == OK, "b's fence");

	/* Забор Mesa: вывоз и ввоз в новый объект. */
	snap = syncobj(ca, USER_A, 0);
	CHECK(so_export(ca, USER_A, so, 1, &s2) == OK && so_import(ca, USER_A,
	    s2, 1, &snap) == OK && model_fd_close(USER_A, s2) == OK &&
	    wait_sync(ca, USER_A, &snap, 1, 0, mono()) == OK, "Mesa's fence");

	/* Слияние: готовый с идущим - идущий; два идущих - оба. */
	sig = syncobj(ca, USER_A, 1);
	CHECK(so_export(ca, USER_A, sig, 1, &fsig) == OK && sel(fsig, USER_A,
	    CDEV_OP_RD) == CDEV_OP_RD, "a signaled sync file is ready");
	chain_to(&a, a.va, 2, 0x12);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, so, 0) == OK &&
	    so_export(ca, USER_A, so, 1, &frun) == OK, "a running one");
	CHECK(merge(fsig, frun, USER_A, &fm) == OK && info(fm, USER_A, &st, &n)
	    == OK && st == 0 && n == 1, "merged: status %d, %u fence(s)", st,
	    n);
	chain_to(&b, b.va, 2, 0x24);
	CHECK(submit(cb, USER_A, b.va, &b.handle, 1, NULL, 0, sout,
	    PANFROST_JD_REQ_FS) == OK && so_export(cb, USER_A, sout, 1, &frun2)
	    == OK, "b's running one");
	CHECK(merge(fm, frun2, USER_A, &d) == OK && info(d, USER_A, &st, &n) ==
	    OK && st == 0 && n == 2, "two running: status %d, %u fence(s)", st,
	    n);
	CHECK(sel(d, USER_A, CDEV_OP_RD) == 0 && run(1, 2) &&
	    model_select_reply(model_fd_minor(USER_A, d)) == 0, "ready with "
	    "one of two over");
	CHECK(run(0, 2) && model_select_reply(model_fd_minor(USER_A, d)) ==
	    CDEV_OP_RD && info(d, USER_A, &st, &n) == OK && st == 1,
	    "both over: status %d", st);
	pipe_fd = model_fd_foreign(USER_A);
	CHECK(merge(fsig, pipe_fd, USER_A, &i) == ENOENT, "merge with a pipe");

	/* Не sync file. */
	CHECK(so_import(ca, USER_A, pipe_fd, 1, &snap) == EINVAL, "a pipe as "
	    "a sync file");
	CHECK(so_import(ca, USER_A, -1, 1, &snap) == EINVAL, "-1 as a sync "
	    "file");
	h99 = 99;
	CHECK(so_import(ca, USER_A, fsig, 1, &h99) == ENOENT, "into sync object "
	    "99");
	CHECK(export_bo(ca, USER_A, a.handle, DRM_CLOEXEC, &d) == OK &&
	    so_import(ca, USER_A, d, 1, &snap) == EINVAL, "a dma-buf as a sync "
	    "file");
	CHECK(model_fd_close(USER_A, d) == OK, "close");

	/* Объект синхронизации целиком: общий для двух контекстов. */
	x = syncobj(ca, USER_A, 0);
	y = 0;
	CHECK(so_export(ca, USER_A, x, 0, &fo) == OK && so_import(cb, USER_A,
	    fo, 0, &y) == OK && y != 0, "a sync object to b: handle %u", y);
	CHECK(so_export(ca, USER_A, x, 0, &d) == OK && model_fd_minor(USER_A,
	    d) == model_fd_minor(USER_A, fo), "exported again: the same minor");
	CHECK(model_fd_close(USER_A, d) == OK, "close");
	CHECK(sync_array(ca, USER_A, DRM_IOCTL_SYNCOBJ_SIGNAL, x) == OK &&
	    wait_sync(cb, USER_A, &y, 1, 0, mono()) == OK, "signalled in a, "
	    "over in b");
	CHECK(sync_array(cb, USER_A, DRM_IOCTL_SYNCOBJ_RESET, y) == OK &&
	    wait_sync(ca, USER_A, &x, 1, 0, mono()) == EINVAL, "reset in b, "
	    "empty in a");
	z = 0;
	CHECK(so_import(ca, USER_A, fo, 0, &z) == OK && z != x, "into a "
	    "itself: a new handle %u", z);
	z = 0;
	CHECK(so_import(ca, USER_A, fsig, 0, &z) == EINVAL, "a sync file as a "
	    "sync object");
	{
		struct drm_syncobj_transfer t;

		memset(&t, 0, sizeof(t));
		CHECK(io(ca, DRM_IOCTL_SYNCOBJ_TRANSFER, &t, USER_A) ==
		    EOPNOTSUPP, "transfer");
	}

	/* Упавшее задание: его sync file - готов, со своей ошибкой. */
	chain_to(&a, a.va, 2, 0x13);
	model_fail(1, 0x58);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, so, 0) == OK &&
	    so_export(ca, USER_A, so, 1, &d) == OK, "a failing job");
	CHECK(model_run(1) == 0 && model_line(MALI_IRQ_JOB) == 1, "it failed");
	CHECK(info(d, USER_A, &st, &n) == OK && st == -EIO, "status %d (%d)",
	    st, -EIO);
	sin = syncobj(cb, USER_A, 0);
	chain_to(&b, b.va, 2, 0x25);
	CHECK(so_import(cb, USER_A, d, 1, &sin) == OK && submit(cb, USER_A,
	    b.va, &b.handle, 1, &sin, 1, 0, PANFROST_JD_REQ_FS) == OK &&
	    model_slot_pending(0), "a job after the failed one runs");
	CHECK(run(0, 2), "it ran");

	/* Контекст уходит с заданием, которое стоит в службе. */
	chain_to(&a, a.va, 2, 0x14);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, so, 0) == OK &&
	    so_export(ca, USER_A, so, 1, &frun) == OK, "a's job");
	sin = syncobj(cb, USER_A, 0);
	CHECK(so_import(cb, USER_A, frun, 1, &sin) == OK && submit(cb, USER_A,
	    b.va, &b.handle, 1, &sin, 1, sout, PANFROST_JD_REQ_FS) == OK &&
	    !model_slot_pending(0), "b's job held");
	CHECK(mali_chardriver.cdr_close(cb) == OK, "b closed");
	CHECK(run(1, 2) && !model_slot_pending(0), "a's job; b's held one "
	    "gone with b");

	/* Пределы: дескрипторов-объектов MAX_OBJ; minor-ы переиспользуются. */
	for (i = 0, r = OK; i < 300 && r == OK; i++)
		if ((r = so_export(ca, USER_A, sig, 1, &d)) == OK)
			r = model_fd_close(USER_A, d);
	CHECK(r == OK, "300 descriptors one after another: %d at %d", r, i);
	CHECK(model_minors_seen() < 16, "%u minors seen for sequential "
	    "descriptors", model_minors_seen());
	{
		int fds[230], k, before = (int)model_files_open();

		r = OK;
		for (k = 0; k < 230; k++)
			if ((r = so_export(ca, USER_A, sig, 1, &fds[k])) != OK)
				break;
		CHECK(r == ENFILE && k + before == 224, "%d descriptors with %d "
		    "open, then %d", k, before, r);
		while (k-- > 0)
			(void)model_fd_close(USER_A, fds[k]);
	}

	(void)ab;
	(void)hs;
	(void)fsig;
	model_proc_exit(USER_A);
	CHECK(mali_chardriver.cdr_close(ca) == OK, "a closed");
	all_gone("sync file");
}

/* ---- ioctl самого dma-buf ------------------------------------------------- */

static void
test_dmabuf(void)
{
	struct dma_buf_sync ds;
	struct dma_buf_export_sync_file ex;
	struct dma_buf_import_sync_file im;
	struct bo a, s;
	uint32_t so, sos;
	int ca, cs, d, m, sf, frun, st, rr;
	unsigned n;
	cdev_id_t wid;

	printf("dma-buf: доступ процессора, заборы буфера\n");
	start();
	ca = open_ctx(USER_A);
	cs = open_ctx(USER_A);
	CHECK(create(ca, USER_A, 0x4000, 0, &a) == OK && map(ca, USER_A, &a) ==
	    OK, "a");
	CHECK(create(cs, USER_A, 0x4000, 0, &s) == OK && map(cs, USER_A, &s) ==
	    OK, "s");
	CHECK(export_bo(ca, USER_A, a.handle, DRM_CLOEXEC | DRM_RDWR, &d) == OK,
	    "export");
	m = model_fd_minor(USER_A, d);

	ds.flags = DMA_BUF_SYNC_READ;
	CHECK(io(m, DMA_BUF_IOCTL_SYNC, &ds, USER_A) == OK, "start, idle");
	ds.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW;
	CHECK(io(m, DMA_BUF_IOCTL_SYNC, &ds, USER_A) == OK, "end");
	ds.flags = 0;
	CHECK(io(m, DMA_BUF_IOCTL_SYNC, &ds, USER_A) == EINVAL, "no direction");
	ds.flags = 8;
	CHECK(io(m, DMA_BUF_IOCTL_SYNC, &ds, USER_A) == EINVAL, "flags 8");
	CHECK(io(m, DRM_IOCTL_GET_CAP, &ds, USER_A) == ENOTTY, "a DRM ioctl on "
	    "a dma-buf");

	/* Занятый: начало доступа ждёт задания, select - тоже. */
	so = syncobj(ca, USER_A, 0);
	chain_to(&a, a.va, 4, 0x31);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, so, 0) == OK,
	    "a's job");
	CHECK(mali_chardriver.cdr_select(m, CDEV_OP_RD | CDEV_NOTIFY, VFS_EP) ==
	    0, "a busy dma-buf readable");
	memset(&ex, 0, sizeof(ex));
	ex.flags = DMA_BUF_SYNC_READ;
	CHECK(io(m, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex, USER_A) == OK &&
	    info(ex.fd, USER_A, &st, &n) == OK && st == 0 && n == 1, "its jobs' "
	    "sync file: status %d, %u", st, n);
	sf = ex.fd;
	ex.flags = 0;
	CHECK(io(m, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex, USER_A) == EINVAL,
	    "export with flags 0");
	ds.flags = DMA_BUF_SYNC_READ;
	CHECK(io(m, DMA_BUF_IOCTL_SYNC, &ds, USER_A) == EDONTREPLY, "start on "
	    "a busy buffer did not wait");
	wid = last_id;
	CHECK(!model_reply(wid, &rr), "answered before the job");
	CHECK(run(1, 4), "the job: %s", why);
	CHECK(model_reply(wid, &rr) && rr == OK, "start answered %d", rr);
	CHECK(model_select_reply(m) == CDEV_OP_RD, "the select told");
	CHECK(info(sf, USER_A, &st, &n) == OK && st == 1, "the sync file over");

	/*
	 * Ввоз забора в буфер: задание другого контекста, его sync file - в
	 * dma-buf; WAIT_BO буфера и следующее задание на нём ждут его.
	 */
	sos = syncobj(cs, USER_A, 0);
	chain_to(&s, s.va, 4, 0x32);
	CHECK(submit(cs, USER_A, s.va, &s.handle, 1, NULL, 0, sos, 0) == OK &&
	    so_export(cs, USER_A, sos, 1, &frun) == OK, "a job of another "
	    "context, its sync file");
	memset(&im, 0, sizeof(im));
	im.flags = DMA_BUF_SYNC_WRITE;
	im.fd = frun;
	CHECK(io(m, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &im, USER_A) == OK,
	    "import into the buffer");
	CHECK(wait_bo(ca, USER_A, a.handle, 0) == EBUSY, "wait_bo did not see "
	    "the imported fence");
	chain_to(&a, a.va, 4, 0x33);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, 0,
	    PANFROST_JD_REQ_FS) == OK && !model_slot_pending(0), "a's job did "
	    "not wait for the imported fence");
	CHECK(run(1, 4) && model_slot_pending(0) && run(0, 4), "then it ran");
	im.fd = model_fd_foreign(USER_A);
	CHECK(io(m, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &im, USER_A) == EINVAL,
	    "a pipe imported");
	im.flags = 0;
	CHECK(io(m, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &im, USER_A) == EINVAL,
	    "flags 0");

	/* Сигнал тому, кто ждёт начала доступа: EINTR. */
	chain_to(&a, a.va, 4, 0x34);
	CHECK(submit(ca, USER_A, a.va, &a.handle, 1, NULL, 0, 0, 0) == OK,
	    "a job");
	ds.flags = DMA_BUF_SYNC_RW;
	CHECK(io(m, DMA_BUF_IOCTL_SYNC, &ds, USER_A) == EDONTREPLY, "waits");
	CHECK(mali_chardriver.cdr_cancel(m, VFS_EP, last_id) == EINTR,
	    "cancelled");
	CHECK(run(1, 4), "the job");

	model_proc_exit(USER_A);
	CHECK(mali_chardriver.cdr_close(ca) == OK && mali_chardriver.cdr_close(
	    cs) == OK, "closed");
	all_gone("dma-buf");
}

int
main(int argc, char *argv[])
{
	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

	test_prime();
	test_syncfile();
	test_dmabuf();

	printf("mali prime: %u проверок, отказов %u\n", checks, failures);
	return failures != 0;
}
