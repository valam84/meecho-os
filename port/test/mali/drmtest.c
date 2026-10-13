/*
 * Стенд устройства службы mali: интерфейс Panfrost (/dev/dri/renderD128),
 * клиенты, их буферы, задания и ожидания - без железа.
 *
 * Собирает настоящие mali_drm.c, mali_mmu.c, mali_bo.c и mali_sched.c на
 * модели (model.c) и зовёт точки входа устройства так, как их звал бы
 * libchardriver по запросам VFS: номер запроса - кодировки машины (её
 * <sys/ioccom.h>, drm.h ветки BSD), аргумент - по гранту ровно того
 * размера, что в номере, массивы и строки - в памяти процесса, от которого
 * вызов (чужая память - нарушение модели).  Клиент видит буфер теми же
 * страницами, что служба (vm_remap() модели отдаёт их же), и пишет в него
 * дескрипторы, как это делал бы Mesa.
 *
 * Что проверяется - кроме ответов на каждый запрос, - то, ради чего
 * интерфейс устроен так, как устроен:
 *
 *   - изоляция: у каждого клиента своё адресное пространство и своё окно;
 *     один и тот же адрес ускорителя у двух клиентов - разные страницы;
 *     отказ одного клиента - ошибка его ожидания, его окно выключено, а
 *     окно и задания другого живут;
 *   - память: новый буфер вычищен в ОЗУ раньше, чем ускоритель может его
 *     прочесть (иначе он увидел бы, что оставил прежний хозяин страницы);
 *     снятый буфер - сначала из таблиц и из TLB окна, потом из памяти
 *     (модель держит TLB: забытый сброс - запись в возвращённую
 *     страницу); закрытый клиент с работающим заданием уходит, только
 *     когда задание кончилось;
 *   - кэш: перед заданием всё, что видел процессор, в ОЗУ; после - снова
 *     из ОЗУ;
 *   - время: ожидание с прошедшим сроком - EBUSY, со сроком - ждёт и
 *     кончается ETIMEDOUT/ETIME; сигнал - EINTR; зависшее задание
 *     останавливается, не остановившееся - сброс ускорителя.
 *
 *	sh run.sh       (собирает и этот стенд)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

#include <minix/cachectl.h>

#include "model.h"
#include "drm.h"
#include "panfrost_drm.h"
#include <sys/ioc_mmap.h>

extern struct chardriver mali_chardriver;

#define RENDER		128
#define USER_A		0x100a
#define USER_B		0x100b
#define VA0		0x02000000ull
#define VALUE(c, i)	((uint32_t)(c) << 24 | (uint32_t)(i))

static cdev_id_t next_id = 1;
static cdev_id_t last_id;

/* Запрос, как его передал бы VFS: грант на аргумент, процесс-вызывающий. */
static int
io(int minor, unsigned long req, void *arg, endpoint_t user)
{
	cp_grant_id_t g = model_grant(arg, IOCPARM_LEN(req));

	last_id = next_id++;
	model_user = user;
	return mali_chardriver.cdr_ioctl(minor, req, VFS_EP, g, 0, user,
	    last_id);
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
	mali_sched_init();
	mali_bo_limits(768u << 20, 512u << 20);
	mali_drm_init();
}

/* Время CLOCK_MONOTONIC, как его видит клиент (PM: boottime + uptime). */
static int64_t
mono(void)
{
	return 1000LL * 1000000000LL + (int64_t)model_now() * 10000000LL;
}

struct bo {
	uint32_t handle;
	uint64_t va;
	char *cpu;		/* как видит клиент */
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

/* MMAP_BO и mmap(2) - то, что libc сделает на ENODEV: MIOCMMAP. */
static int
map(int m, endpoint_t u, struct bo *b, int prot, int flags, size_t len)
{
	struct drm_panfrost_mmap_bo mb;
	struct cdev_mmap cm;
	int r;

	memset(&mb, 0, sizeof(mb));
	mb.handle = b->handle;
	if ((r = io(m, DRM_IOCTL_PANFROST_MMAP_BO, &mb, u)) != OK)
		return r;
	memset(&cm, 0, sizeof(cm));
	cm.cm_len = len ? len : b->size;
	cm.cm_prot = prot;
	cm.cm_flags = flags;
	cm.cm_offset = (off_t)mb.offset;
	if ((r = io(m, MIOCMMAP, &cm, u)) != OK)
		return r;
	b->cpu = cm.cm_ret;
	return OK;
}

static uint32_t
syncobj(int m, endpoint_t u, int signaled)
{
	struct drm_syncobj_create c;
	int r;

	memset(&c, 0, sizeof(c));
	c.flags = signaled ? DRM_SYNCOBJ_CREATE_SIGNALED : 0;
	r = io(m, DRM_IOCTL_SYNCOBJ_CREATE, &c, u);
	CHECK(r == OK && c.handle != 0, "syncobj create: %d", r);
	return c.handle;
}

/* Цепочка из n WRITE_VALUE в буфере: дескрипторы с 0, слова - с 0x1000. */
static void
write_chain(struct bo *b, unsigned n, unsigned tag)
{
	unsigned i;
	char *d;

	for (i = 0; i < n; i++) {
		d = b->cpu + 64 * i;
		memset(d, 0, 64);
		*(uint32_t *)(d + 16) = 1 | (2 << 1) | ((i + 1) << 16);
		*(uint64_t *)(d + 24) = i + 1 < n ? b->va + 64 * (i + 1) : 0;
		*(uint64_t *)(d + 32) = b->va + 0x1000 + 4 * i;
		*(uint32_t *)(d + 40) = 6;
		*(uint64_t *)(d + 48) = VALUE(tag, i);
	}
	memset(b->cpu + 0x1000, 0, 4 * (n + 1));
}

/* Одно WRITE_VALUE на адрес va. */
static void
write_one(struct bo *b, uint64_t va, uint32_t value)
{
	char *d = b->cpu;

	memset(d, 0, 64);
	*(uint32_t *)(d + 16) = 1 | (2 << 1) | (1 << 16);
	*(uint64_t *)(d + 32) = va;
	*(uint32_t *)(d + 40) = 6;
	*(uint64_t *)(d + 48) = value;
}

static unsigned
wrong_words(struct bo *b, unsigned n, unsigned tag)
{
	unsigned i, w = 0;

	for (i = 0; i < n; i++)
		w += ((uint32_t *)(b->cpu + 0x1000))[i] != VALUE(tag, i);
	return w;
}

static int
submit(int m, endpoint_t u, uint64_t jc, uint32_t *bos, unsigned nbos,
	uint32_t out, uint32_t req)
{
	struct drm_panfrost_submit s;

	memset(&s, 0, sizeof(s));
	s.jc = jc;
	s.bo_handles = (uint64_t)(uintptr_t)bos;
	s.bo_handle_count = nbos;
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

static struct drm_syncobj_wait sw;

static int
wait_sync(int m, endpoint_t u, uint32_t *h, unsigned n, uint32_t flags,
	int64_t deadline)
{
	memset(&sw, 0, sizeof(sw));
	sw.handles = (uint64_t)(uintptr_t)h;
	sw.count_handles = n;
	sw.flags = flags;
	sw.timeout_nsec = deadline;
	return io(m, DRM_IOCTL_SYNCOBJ_WAIT, &sw, u);
}

/* ---- интерфейс --------------------------------------------------------- */

static void
test_interface(void)
{
	struct drm_version v;
	struct drm_get_cap cap;
	struct drm_panfrost_get_param p;
	struct drm_panfrost_get_bo_offset off;
	struct drm_panfrost_madvise mad;
	struct drm_gem_close gc;
	struct bo b[8];
	char name[32], date[32], desc[32];
	int a, a2, r;

	printf("интерфейс: версия, параметры, буферы, их места\n");
	start();
	a = open_ctx(USER_A);
	a2 = open_ctx(USER_A);
	CHECK(a == 0 && a2 == 1, "minors %d %d", a, a2);
	CHECK(mali_chardriver.cdr_open(5, 0, USER_A) == ENXIO, "a minor other "
	    "than the render node opened");

	memset(&v, 0, sizeof(v));
	memset(name, 0, sizeof(name));
	memset(date, 0, sizeof(date));
	memset(desc, 0, sizeof(desc));
	v.name = name;
	v.name_len = sizeof(name);
	v.date = date;
	v.date_len = sizeof(date);
	v.desc = desc;
	v.desc_len = sizeof(desc);
	r = io(a, DRM_IOCTL_VERSION, &v, USER_A);
	CHECK(r == OK && v.version_major == 1 && v.version_minor == 2 &&
	    strcmp(name, "panfrost") == 0 && strcmp(date, "20180908") == 0 &&
	    strcmp(desc, "panfrost DRM") == 0 && v.name_len == 8,
	    "version: %d %d.%d '%s' '%s' '%s'", r, v.version_major,
	    v.version_minor, name, date, desc);
	memset(name, 0, sizeof(name));
	v.name_len = 4;
	v.date = NULL;
	v.desc = NULL;
	r = io(a, DRM_IOCTL_VERSION, &v, USER_A);
	CHECK(r == OK && strcmp(name, "panf") == 0 && v.name_len == 8 &&
	    v.date_len == 8, "short name: %d '%s' %zu", r, name,
	    (size_t)v.name_len);
	v.name = (char *)0x10;
	v.name_len = 8;
	CHECK(io(a, DRM_IOCTL_VERSION, &v, USER_A) == EFAULT, "a bad pointer "
	    "for the name was taken");

	memset(&cap, 0, sizeof(cap));
	cap.capability = DRM_CAP_SYNCOBJ;
	CHECK(io(a, DRM_IOCTL_GET_CAP, &cap, USER_A) == OK && cap.value == 1,
	    "syncobj cap");
	cap.capability = DRM_CAP_SYNCOBJ_TIMELINE;
	CHECK(io(a, DRM_IOCTL_GET_CAP, &cap, USER_A) == OK && cap.value == 0,
	    "timeline cap");
	/* ref-api.txt: an unknown capability is ENOTSUP under panfrost. */
	cap.capability = 0x7777;
	CHECK(io(a, DRM_IOCTL_GET_CAP, &cap, USER_A) == EOPNOTSUPP, "odd cap");

	/* Параметры: из регистров, как у panfrost (числа - ref-on.txt). */
	mali.gpu_id = 0x74021000;
	G(GPU_SHADER_PRESENT_LO) = 1;
	G(GPU_L2_PRESENT_LO) = 1;
	G(GPU_TILER_PRESENT_LO) = 1;
	G(GPU_THREAD_MAX_WORKGROUP) = 0x180;
	G(GPU_JS_FEATURES(1)) = 0x20e;
	memset(&p, 0, sizeof(p));
	p.param = DRM_PANFROST_PARAM_GPU_PROD_ID;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == OK &&
	    p.value == 0x7402, "prod id 0x%llx", (unsigned long long)p.value);
	p.param = DRM_PANFROST_PARAM_GPU_REVISION;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == OK &&
	    p.value == 0x1000, "revision 0x%llx", (unsigned long long)p.value);
	p.param = DRM_PANFROST_PARAM_SHADER_PRESENT;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == OK &&
	    p.value == 1, "shader present");
	p.param = DRM_PANFROST_PARAM_AS_PRESENT;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == OK &&
	    p.value == 0xff, "as present");
	p.param = DRM_PANFROST_PARAM_THREAD_MAX_WORKGROUP_SZ;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == OK &&
	    p.value == 0x180, "workgroup");
	p.param = DRM_PANFROST_PARAM_JS_FEATURES1;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == OK &&
	    p.value == 0x20e, "js features 1");
	p.param = DRM_PANFROST_PARAM_NR_CORE_GROUPS;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == OK &&
	    p.value == 1, "core groups");
	p.param = DRM_PANFROST_PARAM_SYSTEM_TIMESTAMP;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == EINVAL,
	    "a parameter of a later panfrost answered");
	p.param = 0;
	p.pad = 1;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_PARAM, &p, USER_A) == EINVAL,
	    "pad not checked");

	/*
	 * Места буферов - по правилам panfrost (drm_mm, лучший подходящий,
	 * код - выровнен на свой размер, не код от 2 МиБ - на 2 МиБ).
	 */
	CHECK(create(a, USER_A, 0x4000, 0, &b[0]) == OK && b[0].va == VA0 &&
	    b[0].handle == 1, "first: 0x%llx handle %u",
	    (unsigned long long)b[0].va, b[0].handle);
	CHECK(create(a, USER_A, 0x1000, PANFROST_BO_NOEXEC, &b[1]) == OK &&
	    b[1].va == 0x2004000, "4K noexec: 0x%llx",
	    (unsigned long long)b[1].va);
	CHECK(create(a, USER_A, 0x300000, PANFROST_BO_NOEXEC, &b[2]) == OK &&
	    b[2].va == 0x2200000, "3M noexec: 0x%llx",
	    (unsigned long long)b[2].va);
	CHECK(create(a, USER_A, 0x19000, 0, &b[3]) == OK &&
	    b[3].va == 0x2008000, "100K code: 0x%llx",
	    (unsigned long long)b[3].va);
	CHECK(create(a, USER_A, 0x100000, PANFROST_BO_NOEXEC |
	    PANFROST_BO_HEAP, &b[4]) == OK && b[4].va == 0x2600000,
	    "heap 1M: 0x%llx", (unsigned long long)b[4].va);
	CHECK(create(a, USER_A, 5000, PANFROST_BO_NOEXEC, &b[7]) == OK &&
	    b[7].va == 0x2005000 && b[7].handle == 6, "5000 bytes: 0x%llx "
	    "handle %u", (unsigned long long)b[7].va, b[7].handle);
	CHECK(create(a2, USER_A, 0x4000, 0, &b[5]) == OK && b[5].va == VA0 &&
	    b[5].handle == 1, "another context's first: 0x%llx",
	    (unsigned long long)b[5].va);

	CHECK(create(a, USER_A, 0, 0, &b[6]) == EINVAL, "size 0");
	CHECK(create(a, USER_A, 0x1000, PANFROST_BO_HEAP, &b[6]) == EINVAL,
	    "an executable heap");
	CHECK(create(a, USER_A, 0x1000, 8, &b[6]) == EINVAL, "flag 8");

	memset(&off, 0, sizeof(off));
	off.handle = b[3].handle;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &off, USER_A) == OK &&
	    off.offset == b[3].va, "offset");
	off.handle = 99;
	CHECK(io(a, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &off, USER_A) == ENOENT,
	    "offset of nothing");

	/* Отображение: весь буфер, общий; куча - нет. */
	CHECK(map(a, USER_A, &b[0], PROT_READ | PROT_WRITE, MAP_SHARED, 0) ==
	    OK && b[0].cpu != NULL && nremaps == 1 && remaps[0].who == USER_A &&
	    remaps[0].len == 0x4000 && !remaps[0].ro, "map: %u remap(s)",
	    nremaps);
	CHECK(map(a, USER_A, &b[1], PROT_READ, MAP_SHARED, 0) == OK &&
	    nremaps == 2 && remaps[1].ro, "read-only map");
	CHECK(map(a, USER_A, &b[0], PROT_READ | PROT_WRITE, MAP_SHARED,
	    0x1000) == EINVAL, "a part of a buffer mapped");
	CHECK(map(a, USER_A, &b[0], PROT_READ | PROT_WRITE, MAP_PRIVATE, 0) ==
	    EINVAL, "a private map");
	CHECK(map(a, USER_A, &b[4], PROT_READ | PROT_WRITE, MAP_SHARED, 0) ==
	    EINVAL, "a heap mapped");
	CHECK(map(a2, USER_A, &b[3], PROT_READ | PROT_WRITE, MAP_SHARED, 0) ==
	    EINVAL || nremaps == 2, "another context's buffer mapped");

	/* Закрытый дескриптор - его номер и место снова в ходу. */
	memset(&gc, 0, sizeof(gc));
	gc.handle = b[1].handle;
	CHECK(io(a, DRM_IOCTL_GEM_CLOSE, &gc, USER_A) == OK, "close");
	CHECK(io(a, DRM_IOCTL_GEM_CLOSE, &gc, USER_A) == EINVAL, "close twice");
	CHECK(create(a, USER_A, 0x1000, PANFROST_BO_NOEXEC, &b[6]) == OK &&
	    b[6].handle == b[1].handle && b[6].va == b[1].va,
	    "after the close: handle %u at 0x%llx", b[6].handle,
	    (unsigned long long)b[6].va);

	memset(&mad, 0, sizeof(mad));
	mad.handle = b[0].handle;
	mad.madv = PANFROST_MADV_DONTNEED;
	CHECK(io(a, DRM_IOCTL_PANFROST_MADVISE, &mad, USER_A) == OK &&
	    mad.retained == 1, "madvise");
	/* ref-api.txt: panfrost takes any advice; retained 1. */
	mad.madv = 7;
	mad.retained = 0;
	CHECK(io(a, DRM_IOCTL_PANFROST_MADVISE, &mad, USER_A) == OK &&
	    mad.retained == 1, "madvise 7");
	mad.handle = 77;
	mad.madv = 0;
	CHECK(io(a, DRM_IOCTL_PANFROST_MADVISE, &mad, USER_A) == ENOENT,
	    "madvise of nothing");

	CHECK(io(a, _IOWR('d', 0x99, int), &r, USER_A) == EINVAL,
	    "a driver ioctl it does not have");
	CHECK(io(a, _IOWR('x', 1, int), &r, USER_A) == ENOTTY,
	    "another group");

	CHECK(mali_chardriver.cdr_close(a) == OK &&
	    mali_chardriver.cdr_close(a2) == OK, "close contexts");
	CHECK(model_live_pages() == 0, "%u page(s) left after both closed",
	    model_live_pages());
	CHECK(model_errors == 0 && vircopy_foreign == 0, "model: %s",
	    model_error_text);
}

/* ---- задания ---------------------------------------------------------- */

static void
test_jobs(void)
{
	struct bo b;
	uint32_t h, so, so2, hs[2];
	unsigned c0, c1;
	cdev_id_t wid;
	int a, r, rr;

	printf("задания одного клиента: цепочка, ожидания, сроки, сигнал\n");
	start();
	a = open_ctx(USER_A);
	CHECK(create(a, USER_A, 0x4000, 0, &b) == OK, "create");
	CHECK(map(a, USER_A, &b, PROT_READ | PROT_WRITE, MAP_SHARED, 0) == OK,
	    "map");
	h = b.handle;
	so = syncobj(a, USER_A, 0);

	/* Ожидание объекта без забора: EINVAL; ожидание буфера без заданий: 0. */
	CHECK(wait_sync(a, USER_A, &so, 1, 0, mono() + 1000000000LL) ==
	    EINVAL, "a wait for a fence that is not there");
	CHECK(wait_bo(a, USER_A, h, 0) == OK, "an idle buffer is busy");

	write_chain(&b, 64, 0x5a);
	c0 = ncops;
	r = submit(a, USER_A, b.va, &h, 1, so, 0);
	CHECK(r == OK, "submit: %d", r);
	CHECK(model_slot_pending(1) && (G(JS_CONFIG(1)) & 0xf) == 0,
	    "not on slot 1 in window 0");
	CHECK(covered(CACHE_CLEAN, b.cpu, b.size, c0), "what the client wrote "
	    "was not cleaned before the job");
	CHECK(wait_bo(a, USER_A, h, 0) == EBUSY, "a busy buffer with a "
	    "deadline past: not EBUSY");
	r = wait_bo(a, USER_A, h, mono() + 2000000000LL);
	wid = last_id;
	CHECK(r == EDONTREPLY, "a wait for a busy buffer: %d", r);

	CHECK(model_run(1) == 64, "the model ran: %s", why);
	c1 = ncops;
	CHECK(model_line(MALI_IRQ_JOB) == 1, "no job line");
	CHECK(model_reply(wid, &rr) && rr == OK, "the wait for the buffer: "
	    "%d", rr);
	CHECK(covered(CACHE_INVALIDATE, b.cpu, b.size, c1), "the client "
	    "reads what its cache had before the job");
	CHECK(wrong_words(&b, 64, 0x5a) == 0, "%u wrong word(s)",
	    wrong_words(&b, 64, 0x5a));
	CHECK(wait_sync(a, USER_A, &so, 1, 0, mono()) == OK &&
	    sw.first_signaled == 0, "the job's fence");

	/* Срок, который выходит: WAIT_BO - ETIMEDOUT, объект - ETIME. */
	write_chain(&b, 4, 0x11);
	CHECK(submit(a, USER_A, b.va, &h, 1, so, 0) == OK, "submit 2");
	r = wait_bo(a, USER_A, h, mono() + 50000000LL);
	wid = last_id;
	CHECK(r == EDONTREPLY, "wait: %d", r);
	model_advance(6);
	CHECK(model_reply(wid, &rr) && rr == ETIMEDOUT, "deadline: %d", rr);
	CHECK(wait_sync(a, USER_A, &so, 1, 0, mono()) == ETIME, "fence not "
	    "ETIME");
	CHECK(model_run(1) == 4 && model_line(MALI_IRQ_JOB) == 1, "job 2");

	/* WAIT_FOR_SUBMIT: забор появится от следующего SUBMIT. */
	so2 = syncobj(a, USER_A, 0);
	r = wait_sync(a, USER_A, &so2, 1, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
	    mono() + 3000000000LL);
	wid = last_id;
	CHECK(r == EDONTREPLY, "wait for submit: %d", r);
	write_chain(&b, 2, 0x22);
	CHECK(submit(a, USER_A, b.va, &h, 1, so2, 0) == OK, "submit 3");
	CHECK(!model_reply(wid, &rr), "answered before the job ended");
	CHECK(model_run(1) == 2 && model_line(MALI_IRQ_JOB) == 1, "job 3");
	CHECK(model_reply(wid, &rr) && rr == OK, "wait for submit: %d", rr);

	/* Сброс, сигнал, удаление, все сразу. */
	CHECK(io(a, DRM_IOCTL_SYNCOBJ_RESET, &(struct drm_syncobj_array){
	    .handles = (uint64_t)(uintptr_t)&so, .count_handles = 1 }, USER_A)
	    == OK, "reset");
	CHECK(wait_sync(a, USER_A, &so, 1, 0, mono()) == EINVAL, "after reset");
	CHECK(io(a, DRM_IOCTL_SYNCOBJ_SIGNAL, &(struct drm_syncobj_array){
	    .handles = (uint64_t)(uintptr_t)&so, .count_handles = 1 }, USER_A)
	    == OK, "signal");
	hs[0] = so;
	hs[1] = so2;
	CHECK(wait_sync(a, USER_A, hs, 2, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
	    mono()) == OK, "both");
	CHECK(io(a, DRM_IOCTL_SYNCOBJ_DESTROY, &(struct drm_syncobj_destroy){
	    .handle = so }, USER_A) == OK, "destroy");
	CHECK(wait_sync(a, USER_A, &so, 1, 0, mono()) == ENOENT, "destroyed");

	/* Сигнал процессу, который ждёт: EINTR, и больше ничего. */
	write_chain(&b, 2, 0x33);
	CHECK(submit(a, USER_A, b.va, &h, 1, 0, 0) == OK, "submit 4");
	r = wait_bo(a, USER_A, h, INT64_MAX);
	wid = last_id;
	CHECK(r == EDONTREPLY, "wait 4: %d", r);
	CHECK(mali_chardriver.cdr_cancel(a, VFS_EP, wid) == EINTR, "cancel");
	CHECK(mali_chardriver.cdr_cancel(a, VFS_EP, wid) == EDONTREPLY,
	    "cancel twice");
	CHECK(model_run(1) == 2 && model_line(MALI_IRQ_JOB) == 1 &&
	    !model_reply(wid, &rr), "a cancelled wait answered");

	/* Ошибки SUBMIT. */
	CHECK(submit(a, USER_A, 0, &h, 1, 0, 0) == EINVAL, "jc 0");
	hs[0] = 99;
	CHECK(submit(a, USER_A, b.va, hs, 1, 0, 0) == ENOENT, "no such bo");
	CHECK(submit(a, USER_A, b.va, (uint32_t *)0x10, 1, 0, 0) == EFAULT,
	    "a bad array");
	CHECK(submit(a, USER_A, b.va, &h, 1, 77, 0) == ENODEV, "no such "
	    "out_sync");
	CHECK(submit(a, USER_A, b.va, &h, 1, 0, 4) == EINVAL, "requirement 4");
	{
		/* SUBMIT старого размера (40 байт, ядро 6.1): принят. */
		struct drm_panfrost_submit s;

		memset(&s, 0, sizeof(s));
		write_chain(&b, 3, 0x44);
		s.jc = b.va;
		s.bo_handles = (uint64_t)(uintptr_t)&h;
		s.bo_handle_count = 1;
		r = io(a, _IOC(IOC_IN, DRM_IOCTL_BASE, DRM_COMMAND_BASE +
		    DRM_PANFROST_SUBMIT, 40), &s, USER_A);
		CHECK(r == OK && model_run(1) == 3 &&
		    model_line(MALI_IRQ_JOB) == 1 && wrong_words(&b, 3, 0x44) ==
		    0, "a 40-byte submit: %d", r);
	}

	CHECK(mali_chardriver.cdr_close(a) == OK, "close");
	CHECK(model_live_pages() == 0, "%u page(s) left", model_live_pages());
	CHECK(model_errors == 0 && vircopy_foreign == 0, "model: %s",
	    model_error_text);
	CHECK(!model_timer_pending(), "a timer left");
}

/* ---- два клиента ------------------------------------------------------- */

static void
test_two(void)
{
	struct bo ba, bb;
	uint32_t ha, hb, sa, sb;
	uint64_t tta, ttb;
	int a, b;

	printf("два клиента: свои пространства, свои окна, один адрес\n");
	start();
	a = open_ctx(USER_A);
	b = open_ctx(USER_B);
	CHECK(create(a, USER_A, 0x4000, 0, &ba) == OK && create(b, USER_B,
	    0x4000, 0, &bb) == OK && ba.va == VA0 && bb.va == VA0,
	    "both at 0x2000000");
	CHECK(map(a, USER_A, &ba, PROT_READ | PROT_WRITE, MAP_SHARED, 0) ==
	    OK && map(b, USER_B, &bb, PROT_READ | PROT_WRITE, MAP_SHARED, 0) ==
	    OK && ba.cpu != bb.cpu, "maps");
	ha = ba.handle;
	hb = bb.handle;
	sa = syncobj(a, USER_A, 0);
	sb = syncobj(b, USER_B, 0);

	/* Одним слотом: второй ждёт первого, потом идёт в своём окне. */
	write_chain(&ba, 64, 0xa1);
	write_chain(&bb, 64, 0xb2);
	CHECK(submit(a, USER_A, ba.va, &ha, 1, sa, 0) == OK &&
	    submit(b, USER_B, bb.va, &hb, 1, sb, 0) == OK, "submits");
	CHECK((G(JS_CONFIG(1)) & 0xf) == 0, "a's job not in window 0");
	tta = model_as_active(0);
	CHECK(model_run(1) == 64 && model_line(MALI_IRQ_JOB) == 1, "a ran");
	CHECK(model_slot_pending(1) && (G(JS_CONFIG(1)) & 0xf) == 1,
	    "b's job not in window 1 after a's (config 0x%x)",
	    G(JS_CONFIG(1)));
	ttb = model_as_active(1);
	CHECK((tta & 3) == 3 && (ttb & 3) == 3 && (tta & ~0xfffull) !=
	    (ttb & ~0xfffull), "the two windows share tables: 0x%llx 0x%llx",
	    (unsigned long long)tta, (unsigned long long)ttb);
	CHECK(model_run(1) == 64 && model_line(MALI_IRQ_JOB) == 1, "b ran");
	CHECK(wrong_words(&ba, 64, 0xa1) == 0 && wrong_words(&bb, 64, 0xb2) ==
	    0, "words: a %u b %u", wrong_words(&ba, 64, 0xa1),
	    wrong_words(&bb, 64, 0xb2));
	CHECK(wait_sync(a, USER_A, &sa, 1, 0, mono()) == OK &&
	    wait_sync(b, USER_B, &sb, 1, 0, mono()) == OK, "fences");

	/* Разными слотами (фрагментное у b): оба сразу. */
	write_chain(&ba, 8, 0xa3);
	write_chain(&bb, 8, 0xb4);
	CHECK(submit(a, USER_A, ba.va, &ha, 1, sa, 0) == OK &&
	    submit(b, USER_B, bb.va, &hb, 1, sb, PANFROST_JD_REQ_FS) == OK,
	    "submits 2");
	CHECK(model_slot_pending(1) && model_slot_pending(0) &&
	    (G(JS_CONFIG(1)) & 0xf) == 0 && (G(JS_CONFIG(0)) & 0xf) == 1,
	    "not side by side: 0x%x 0x%x", G(JS_CONFIG(1)), G(JS_CONFIG(0)));
	CHECK(model_run(0) == 8 && model_run(1) == 8 &&
	    model_line(MALI_IRQ_JOB) == 1, "both ran");
	CHECK(wrong_words(&ba, 8, 0xa3) == 0 && wrong_words(&bb, 8, 0xb4) == 0,
	    "words 2");

	/* Задания одного клиента - по одному и по порядку, хоть и в разные слоты. */
	write_chain(&ba, 4, 0xa5);
	CHECK(submit(a, USER_A, ba.va, &ha, 1, 0, 0) == OK &&
	    submit(a, USER_A, ba.va, &ha, 1, 0, PANFROST_JD_REQ_FS) == OK,
	    "two of a's");
	CHECK(model_slot_pending(1) && !model_slot_pending(0), "a's second "
	    "job started before its first ended");
	CHECK(model_run(1) == 4 && model_line(MALI_IRQ_JOB) == 1 &&
	    model_slot_pending(0), "a's second did not follow");
	CHECK(model_run(0) == 4 && model_line(MALI_IRQ_JOB) == 1, "a's second");

	CHECK(mali_chardriver.cdr_close(a) == OK &&
	    mali_chardriver.cdr_close(b) == OK, "close");
	CHECK(model_live_pages() == 0, "%u page(s) left", model_live_pages());
	CHECK(model_errors == 0 && vircopy_foreign == 0, "model: %s",
	    model_error_text);
}

/* ---- отказ одного клиента ------------------------------------------------- */

static void
test_fault(int at_once)
{
	struct bo ba, bb;
	uint32_t ha, hb, sa, sb;
	int a, b, r;

	printf("отказ клиента%s: его ожидание - EIO, другой живёт\n",
	    at_once ? " (задание падает сразу)" : "");
	start();
	model_fail_at_once(at_once);
	a = open_ctx(USER_A);
	b = open_ctx(USER_B);
	CHECK(create(a, USER_A, 0x4000, 0, &ba) == OK && create(b, USER_B,
	    0x4000, 0, &bb) == OK, "create");
	CHECK(map(a, USER_A, &ba, PROT_READ | PROT_WRITE, MAP_SHARED, 0) ==
	    OK && map(b, USER_B, &bb, PROT_READ | PROT_WRITE, MAP_SHARED, 0) ==
	    OK, "maps");
	ha = ba.handle;
	hb = bb.handle;
	sa = syncobj(a, USER_A, 0);
	sb = syncobj(b, USER_B, 0);

	/* a пишет туда, где у него ничего нет; b - цепочку рядом. */
	write_one(&ba, VA0 + 0x10040, 0xbadc0de5);
	write_chain(&bb, 64, 0xb6);
	CHECK(submit(a, USER_A, ba.va, &ha, 1, sa, 0) == OK &&
	    submit(b, USER_B, bb.va, &hb, 1, sb, PANFROST_JD_REQ_FS) == OK,
	    "submits");
	CHECK(model_run(1) == 0 && G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0),
	    "a did not fault: raw 0x%x (%s)", G(MMU_INT_RAWSTAT), why);
	CHECK(model_run(0) == 64, "b: %s", why);
	if (at_once) {
		CHECK(model_line(MALI_IRQ_JOB) == 1, "job line");
		CHECK(wait_sync(a, USER_A, &sa, 1, 0, mono()) == ETIME,
		    "a's job over before the MMU line");
		CHECK(model_line(MALI_IRQ_MMU) == 1, "mmu line");
	} else {
		CHECK(model_line(MALI_IRQ_MMU) == 1, "mmu line");
		CHECK(model_line(MALI_IRQ_JOB) == 1, "job line");
	}
	CHECK(said("line 73 (mmu)") && said("at 0x0000000002010040, in "
	    "client 0") && said("client 0: the job at 0x02000000 failed, slot "
	    "status 0x48; GPU address space fault 0x7c1003c3"),
	    "the log: %s", logbuf);
	r = wait_sync(a, USER_A, &sa, 1, 0, mono());
	CHECK(r == EIO, "a's fence: %d, want EIO", r);
	CHECK(wait_bo(a, USER_A, ha, 0) == OK, "a's buffer after the "
	    "failure: not idle as under Linux");
	CHECK(wait_sync(b, USER_B, &sb, 1, 0, mono()) == OK &&
	    wrong_words(&bb, 64, 0xb6) == 0, "b suffered");
	CHECK((model_as_active(0) & 3) != 3 && (model_as_active(1) & 3) == 3 &&
	    (G(MMU_INT_MASK) & MMU_INT_AS(1)), "windows: a's 0x%llx b's "
	    "0x%llx mask 0x%x", (unsigned long long)model_as_active(0),
	    (unsigned long long)model_as_active(1), G(MMU_INT_MASK));

	/* a живёт дальше: следующая цепочка - в окне снова. */
	write_chain(&ba, 16, 0xa7);
	CHECK(submit(a, USER_A, ba.va, &ha, 1, sa, 0) == OK, "a again");
	CHECK(model_run(1) == 16 && model_line(MALI_IRQ_JOB) == 1, "a again "
	    "ran: %s", why);
	CHECK(wait_sync(a, USER_A, &sa, 1, 0, mono()) == OK &&
	    wrong_words(&ba, 16, 0xa7) == 0, "a's chain after its fault");
	CHECK(G(MMU_INT_RAWSTAT) == 0, "a fault bit left: 0x%x",
	    G(MMU_INT_RAWSTAT));

	CHECK(mali_chardriver.cdr_close(a) == OK &&
	    mali_chardriver.cdr_close(b) == OK, "close");
	CHECK(model_live_pages() == 0, "%u page(s) left", model_live_pages());
	CHECK(model_errors == 0, "model: %s", model_error_text);
}

/* ---- память: снятие, закрытие, новые буферы -------------------------------- */

static void
test_memory(void)
{
	struct bo b, x;
	struct drm_gem_close gc;
	uint32_t h[2];
	unsigned live, mark;
	int a, r;

	printf("память: снятый буфер, закрытый клиент, пределы\n");
	start();
	a = open_ctx(USER_A);
	CHECK(create(a, USER_A, 0x2000, 0, &b) == OK && create(a, USER_A,
	    0x2000, PANFROST_BO_NOEXEC, &x) == OK, "create");
	CHECK(map(a, USER_A, &b, PROT_READ | PROT_WRITE, MAP_SHARED, 0) == OK,
	    "map");
	h[0] = b.handle;
	h[1] = x.handle;

	/* Задание пишет в x: перевод x - в TLB окна. */
	write_one(&b, x.va + 0x100, 0x1234);
	CHECK(submit(a, USER_A, b.va, h, 2, 0, 0) == OK && model_run(1) == 1 &&
	    model_line(MALI_IRQ_JOB) == 1, "the write to x");

	/*
	 * x закрыт: из таблиц и из TLB раньше, чем из памяти.  Следующее
	 * задание, пишущее туда же, обязано отказать, а не попасть в
	 * возвращённую страницу.
	 */
	mark = model_flush_mark();
	memset(&gc, 0, sizeof(gc));
	gc.handle = x.handle;
	CHECK(io(a, DRM_IOCTL_GEM_CLOSE, &gc, USER_A) == OK, "close x");
	CHECK(model_flushed(0, x.va, 0x2000, mark), "x left the tables without "
	    "the window's caches flushed");
	write_one(&b, x.va + 0x100, 0x5678);
	CHECK(submit(a, USER_A, b.va, h, 1, 0, 0) == OK, "submit");
	r = model_run(1);
	CHECK(r == 0 && G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0) &&
	    model_errors == 0, "the GPU reached x after it went: %d, %s", r,
	    model_error_text);
	CHECK(model_line(MALI_IRQ_MMU) == 1 && model_line(MALI_IRQ_JOB) == 1,
	    "lines");

	/*
	 * Буфер через границу 32 КиБ: область замка обязана держать его весь
	 * (здесь - 64 КиБ), иначе его верхние страницы останутся в TLB.
	 */
	{
		struct bo z;

		CHECK(create(a, USER_A, 0x8000, PANFROST_BO_NOEXEC, &z) == OK &&
		    z.va == 0x2002000, "z at 0x%llx", (unsigned long long)z.va);
		h[1] = z.handle;
		write_one(&b, z.va + 0x7000, 0x77);
		CHECK(submit(a, USER_A, b.va, h, 2, 0, 0) == OK &&
		    model_run(1) == 1 && model_line(MALI_IRQ_JOB) == 1,
		    "the write to z: %s", why);
		mark = model_flush_mark();
		gc.handle = z.handle;
		CHECK(io(a, DRM_IOCTL_GEM_CLOSE, &gc, USER_A) == OK, "close z");
		CHECK(model_flushed(0, z.va, 0x8000, mark), "z left the tables "
		    "and only part of it the window's caches");
		write_one(&b, z.va + 0x7000, 0x78);
		CHECK(submit(a, USER_A, b.va, h, 1, 0, 0) == OK, "submit z");
		r = model_run(1);
		CHECK(r == 0 && G(MMU_INT_RAWSTAT) == MMU_INT_PAGE_FAULT(0) &&
		    model_errors == 0, "the GPU reached z after it went: %d, %s",
		    r, model_error_text);
		CHECK(model_line(MALI_IRQ_MMU) == 1 &&
		    model_line(MALI_IRQ_JOB) == 1, "lines z");
	}

	/* Закрыт с работающим заданием: уходит, когда задание кончилось. */
	write_one(&b, b.va + 0x1000, 0x99);
	CHECK(submit(a, USER_A, b.va, h, 1, 0, 0) == OK, "submit 3");
	live = model_live_pages();
	CHECK(mali_chardriver.cdr_close(a) == OK, "close");
	CHECK(model_live_pages() == live && live > 0, "memory went while the "
	    "job may use it: %u of %u", model_live_pages(), live);
	CHECK(model_run(1) == 1 && model_line(MALI_IRQ_JOB) == 1, "the job");
	CHECK(model_live_pages() == 0 && (model_as_active(0) & 3) != 3,
	    "after the job: %u page(s), window 0x%llx", model_live_pages(),
	    (unsigned long long)model_as_active(0));
	CHECK(mali_chardriver.cdr_open(RENDER, 0, USER_A) == (CDEV_CLONED | 0),
	    "the context did not come free");

	/* Закрыт с заданием в очереди: оно не начнётся. */
	start();
	a = open_ctx(USER_A);
	{
		int c = open_ctx(USER_B);
		struct bo cb;
		uint32_t hc;

		CHECK(create(c, USER_B, 0x2000, 0, &cb) == OK && map(c, USER_B,
		    &cb, PROT_READ | PROT_WRITE, MAP_SHARED, 0) == OK, "c");
		hc = cb.handle;
		write_chain(&cb, 2, 0xc1);
		CHECK(submit(c, USER_B, cb.va, &hc, 1, 0, 0) == OK, "c's job");
		CHECK(create(a, USER_A, 0x2000, 0, &b) == OK && map(a, USER_A,
		    &b, PROT_READ | PROT_WRITE, MAP_SHARED, 0) == OK, "a");
		h[0] = b.handle;
		write_chain(&b, 2, 0xa1);
		CHECK(submit(a, USER_A, b.va, h, 1, 0, 0) == OK, "a's job");
		CHECK(mali_chardriver.cdr_close(a) == OK, "close a");
		CHECK(model_run(1) == 2 && model_line(MALI_IRQ_JOB) == 1 &&
		    !model_slot_pending(1), "a's queued job started");
		CHECK(mali_chardriver.cdr_close(c) == OK, "close c");
		CHECK(model_live_pages() == 0, "%u page(s) left",
		    model_live_pages());
	}

	/* Пределы памяти. */
	start();
	mali_bo_limits(0x10000, 0x8000);
	a = open_ctx(USER_A);
	CHECK(create(a, USER_A, 0x8000, 0, &b) == OK, "within");
	CHECK(create(a, USER_A, 0x1000, 0, &x) == ENOMEM, "past the client's "
	    "limit");
	CHECK(mali_chardriver.cdr_close(a) == OK, "close");
	CHECK(model_errors == 0, "model: %s", model_error_text);
}

/* ---- зависание ------------------------------------------------------------ */

static void
test_hang(void)
{
	struct bo b;
	uint32_t h, so;
	int a;

	printf("зависшее задание: остановка, потом сброс ускорителя\n");
	start();
	a = open_ctx(USER_A);
	CHECK(create(a, USER_A, 0x4000, 0, &b) == OK && map(a, USER_A, &b,
	    PROT_READ | PROT_WRITE, MAP_SHARED, 0) == OK, "create");
	h = b.handle;
	so = syncobj(a, USER_A, 0);

	/* Останавливается остановкой: ETIMEDOUT-задание, забор с ошибкой. */
	write_chain(&b, 2, 0x10);
	model_hang(1, 0);
	CHECK(submit(a, USER_A, b.va, &h, 1, so, 0) == OK, "submit");
	model_advance(100);
	CHECK(G(JS_COMMAND(1)) == JS_COMMAND_HARD_STOP, "no hard stop");
	CHECK(model_line(MALI_IRQ_JOB) == 1, "no job line after the stop");
	CHECK(wait_sync(a, USER_A, &so, 1, 0, mono()) == EIO, "the fence of "
	    "a stopped job");

	/* Не останавливается: через секунду - сброс, задание падает. */
	model_hang(1, 1);
	CHECK(submit(a, USER_A, b.va, &h, 1, so, 0) == OK, "submit 2");
	model_advance(100);
	CHECK(model_recovers == 0, "reset too early");
	model_advance(100);
	CHECK(model_recovers == 1 && said("does not stop"), "no reset: %u",
	    model_recovers);
	CHECK(wait_sync(a, USER_A, &so, 1, 0, mono()) == EIO, "the fence of "
	    "a job lost in the reset");
	CHECK(mali_chardriver.cdr_close(a) == OK, "close");
	CHECK(model_live_pages() == 0, "%u page(s) left", model_live_pages());
	CHECK(model_errors == 0, "model: %s", model_error_text);
}

int
main(int argc, char *argv[])
{
	verbose = argc > 1 && strcmp(argv[1], "-v") == 0;

	test_interface();
	test_jobs();
	test_two();
	test_fault(0);
	test_fault(1);
	test_memory();
	test_hang();

	printf("mali drm: %u проверок, отказов %u\n", checks, failures);
	return failures != 0;
}
