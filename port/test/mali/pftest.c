/*
 * pftest - клиент службы mali на MEECHO: интерфейс Panfrost
 * (/dev/dri/renderD128) так, как им пользуется Mesa, с проверкой
 * процессором.  Веха 11.6.
 *
 * Делает то же, что зонд port/cb2-gpu/jobprobe.py делает через panfrost
 * под вендорским Linux, и печатает то же строка в строку - два вывода
 * сравниваются друг с другом (port/cb2-gpu/ref-api.txt и ref-two.txt
 * против meecho-api.txt и meecho-two.txt):
 *
 *	pftest api	версия, возможности, параметры, места буферов, ошибки,
 *			ожидания со сроками, объекты синхронизации, цепочка из
 *			64 WRITE_VALUE с проверкой, отказ задания, куча
 *	pftest two	два клиента (два процесса, два открытия узла) гоняют
 *			цепочки разом, один на середине - задание с отказом
 *	pftest heap	куча растёт по отказам: запись в первый кусок, в тот же,
 *			одно задание через 16 новых кусков, последний кусок,
 *			за концом кучи (отказ), цепочка после
 *	pftest prime	буферы и заборы дескрипторами: вывоз и ввоз буфера
 *			(PRIME) между контекстами и процессами, sync file -
 *			poll, слияние, задание после чужого задания, ioctl
 *			dma-buf (ref-prime.txt)
 *	pftest chain [n]  одна цепочка из 64, n раз
 *	pftest cost [n]	цена обслуживания кэша: та же цепочка n раз с большим
 *			буфером в списке задания - не отображённым и
 *			отображённым процессором (служба чистит его целиком
 *			до задания и после)
 *
 * Заголовки - libdrm (drm.h) и ядра Linux (panfrost_drm.h), из
 * /usr/include/libdrm, как у Mesa; номера запросов - кодировки MINIX.
 * Отображение буфера - mmap(2) узла; libc сама спрашивает драйвер
 * (MIOCMMAP), когда VM говорит ENODEV.  VFS до 11.6 говорила EINVAL, и
 * тогда pftest спрашивает драйвер сам и пишет об этом строку.
 *
 * Собирается кросс-компилятором против DESTDIR:
 *	aarch64-elf64-minix-gcc -O -static -I$DEST/usr/include/libdrm \
 *	    -idirafter $DEST/usr/include/gfxcompat --sysroot=$DEST \
 *	    -o pftest pftest.c
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ioc_mmap.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "drm.h"
#include "panfrost_drm.h"
#include <linux/dma-buf.h>		/* -idirafter .../gfxcompat */
#include <linux/sync_file.h>

#define NODE		"/dev/dri/renderD128"
#define JOB_SIZE	64
#define FAULT_OFF	0x10040u
/*
 * api: FAULT_OFF from the first buffer lands in the 100 KiB one
 * (0x2008000-0x2021000); 0x2100040 is empty for sure - the same
 * last-level table, between it and the 3 MiB one at 0x2200000.
 */
#define API_FAULT_OFF	0x100040u
#define FAULT_VALUE	0xbadc0de5u

static int direct_mmap_said;

static const char *
ename(int e)
{
	static char buf[16];

	switch (e) {
	case 0:		return "ok";
	case EINVAL:	return "EINVAL";
	case ENOENT:	return "ENOENT";
	case EFAULT:	return "EFAULT";
	case ENODEV:	return "ENODEV";
	case EBUSY:	return "EBUSY";
	case ETIMEDOUT:	return "ETIMEDOUT";
	case ETIME:	return "ETIME";
	case EIO:	return "EIO";
	case ENOMEM:	return "ENOMEM";
	case ENOSPC:	return "ENOSPC";
	case EINTR:	return "EINTR";
	case ENOTTY:	return "ENOTTY";
	case EPERM:	return "EPERM";
	case EACCES:	return "EACCES";
	case EOPNOTSUPP: return "ENOTSUP";	/* Python's name for it */
	case EBADF:	return "EBADF";
	case ENFILE:	return "ENFILE";
	case EMFILE:	return "EMFILE";
	case EAGAIN:	return "EAGAIN";
	}
	snprintf(buf, sizeof(buf), "E%d", e);
	return buf;
}

/* drmIoctl(): повтор на EINTR и EAGAIN; 0 или errno. */
static int
xioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (r == -1 && (errno == EINTR || errno == EAGAIN));
	return r == -1 ? errno : 0;
}

static int64_t
now_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

struct bo {
	uint32_t handle;
	uint64_t va;
	size_t size;
	unsigned char *m;
};

static int
create(int fd, uint32_t size, uint32_t flags, struct bo *b)
{
	struct drm_panfrost_create_bo c;
	int e;

	memset(&c, 0, sizeof(c));
	c.size = size;
	c.flags = flags;
	if ((e = xioctl(fd, DRM_IOCTL_PANFROST_CREATE_BO, &c)) != 0) {
		printf("create 0x%x flags %u: %s\n", size, flags, ename(e));
		return e;
	}
	printf("create 0x%x flags %u: handle %u va 0x%llx\n", size, flags,
	    c.handle, (unsigned long long)c.offset);
	if (b != NULL) {
		b->handle = c.handle;
		b->va = c.offset;
		b->size = (size + 4095) & ~4095u;
		b->m = NULL;
	}
	return 0;
}

/* MMAP_BO, потом mmap(2) - или, если VFS старая, запрос драйверу. */
static int
map_bo(int fd, struct bo *b)
{
	struct drm_panfrost_mmap_bo mb;
	struct cdev_mmap cm;
	void *p;
	int e, r;

	memset(&mb, 0, sizeof(mb));
	mb.handle = b->handle;
	if ((e = xioctl(fd, DRM_IOCTL_PANFROST_MMAP_BO, &mb)) != 0)
		return e;
	p = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
	    (off_t)mb.offset);
	if (p == MAP_FAILED) {
		/*
		 * EINVAL: a VFS from before 11.6 (no ENODEV, so libc never
		 * asked the driver).  Anything else: say it, and what the
		 * driver itself answers.
		 */
		e = errno;
		memset(&cm, 0, sizeof(cm));
		cm.cm_len = b->size;
		cm.cm_prot = PROT_READ | PROT_WRITE;
		cm.cm_flags = MAP_SHARED;
		cm.cm_offset = (off_t)mb.offset;
		r = xioctl(fd, MIOCMMAP, &cm);
		if (!direct_mmap_said++)
			printf("(mmap(2): %s; MIOCMMAP 0x%lx to the driver "
			    "directly: %s)\n", ename(e), (unsigned long)MIOCMMAP,
			    ename(r));
		if (r != 0)
			return r;
		p = cm.cm_ret;
	}
	b->m = p;
	return 0;
}

static void
write_value_job(unsigned char *m, size_t at, unsigned index, uint64_t next,
	uint64_t addr, uint32_t value)
{
	unsigned char *d = m + at;

	memset(d, 0, JOB_SIZE);
	*(uint32_t *)(d + 16) = 1 | (2 << 1) | (index << 16);
	*(uint64_t *)(d + 24) = next;
	*(uint64_t *)(d + 32) = addr;
	*(uint32_t *)(d + 40) = 6;
	*(uint64_t *)(d + 48) = value;
}

static void
chain_into(struct bo *b, unsigned n, unsigned tag)
{
	unsigned i;

	for (i = 0; i < n; i++)
		((uint32_t *)(b->m + 0x1000))[i] = 0;
	for (i = 0; i < n; i++)
		write_value_job(b->m, JOB_SIZE * i, i + 1, i + 1 < n ?
		    b->va + JOB_SIZE * (i + 1) : 0, b->va + 0x1000 + 4 * i,
		    (uint32_t)tag << 24 | i);
}

static unsigned
wrong(struct bo *b, unsigned n, unsigned tag)
{
	unsigned i, w = 0;

	for (i = 0; i < n; i++)
		w += ((uint32_t *)(b->m + 0x1000))[i] != ((uint32_t)tag << 24 |
		    i);
	return w;
}

static int
submit(int fd, uint64_t jc, uint32_t *bos, unsigned n, uint32_t out,
	uint32_t req)
{
	struct drm_panfrost_submit s;

	memset(&s, 0, sizeof(s));
	s.jc = jc;
	s.bo_handles = (uint64_t)(uintptr_t)bos;
	s.bo_handle_count = n;
	s.out_sync = out;
	s.requirements = req;
	return xioctl(fd, DRM_IOCTL_PANFROST_SUBMIT, &s);
}

static const char *
wait_bo(int fd, uint32_t h, int64_t deadline)
{
	struct drm_panfrost_wait_bo w;

	memset(&w, 0, sizeof(w));
	w.handle = h;
	w.timeout_ns = deadline;
	return ename(xioctl(fd, DRM_IOCTL_PANFROST_WAIT_BO, &w));
}

static uint32_t
sync_create(int fd, uint32_t flags)
{
	struct drm_syncobj_create c;

	memset(&c, 0, sizeof(c));
	c.flags = flags;
	if (xioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &c) != 0)
		return 0;
	return c.handle;
}

static const char *
sync_wait(int fd, uint32_t *h, unsigned n, uint32_t flags, int64_t deadline,
	uint32_t *first)
{
	struct drm_syncobj_wait w;
	int e;

	memset(&w, 0, sizeof(w));
	w.handles = (uint64_t)(uintptr_t)h;
	w.count_handles = n;
	w.flags = flags;
	w.timeout_nsec = deadline;
	e = xioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &w);
	if (first != NULL)
		*first = w.first_signaled;
	return ename(e);
}

static const char *
sync_array(int fd, unsigned long req, uint32_t *h, unsigned n)
{
	struct drm_syncobj_array a;

	memset(&a, 0, sizeof(a));
	a.handles = (uint64_t)(uintptr_t)h;
	a.count_handles = n;
	return ename(xioctl(fd, req, &a));
}

static int
open_node(void)
{
	int fd;

	if ((fd = open(NODE, O_RDWR)) < 0) {
		printf("%s: %s\n", NODE, strerror(errno));
		exit(1);
	}
	return fd;
}

/* ---- api ---------------------------------------------------------------- */

static int
api(void)
{
	struct drm_version v;
	struct drm_get_cap cap;
	struct drm_panfrost_get_param p;
	struct drm_panfrost_get_bo_offset off;
	struct drm_panfrost_mmap_bo mb;
	struct drm_panfrost_madvise mad;
	struct drm_gem_close gc;
	struct bo b[8], bl, bh;
	char name[32], date[32], desc[32];
	static const uint64_t caps[] = { 0x13, 0x5, 0x6, 0x14, 0x7777 };
	uint32_t so, so2, hs[2], first, h0, hl;
	int64_t t;
	unsigned i, n, at;
	int fd, e;

	fd = open_node();
	printf("render node %s\n", NODE);

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
	if ((e = xioctl(fd, DRM_IOCTL_VERSION, &v)) != 0)
		printf("version: %s\n", ename(e));
	else
		printf("version: %d.%d.%d name '%s' (%zu) date '%s' (%zu) desc "
		    "'%s' (%zu)\n", v.version_major, v.version_minor,
		    v.version_patchlevel, name, (size_t)v.name_len, date,
		    (size_t)v.date_len, desc, (size_t)v.desc_len);
	memset(&v, 0, sizeof(v));
	memset(name, 0, sizeof(name));
	v.name = name;
	v.name_len = 4;
	e = xioctl(fd, DRM_IOCTL_VERSION, &v);
	printf("version, name buffer of 4: '%s', name_len %zu\n", name,
	    (size_t)v.name_len);

	for (i = 0; i < sizeof(caps) / sizeof(caps[0]); i++) {
		memset(&cap, 0, sizeof(cap));
		cap.capability = caps[i];
		if ((e = xioctl(fd, DRM_IOCTL_GET_CAP, &cap)) != 0)
			printf("cap 0x%llx: %s\n", (unsigned long long)caps[i],
			    ename(e));
		else
			printf("cap 0x%llx: %llu\n", (unsigned long long)caps[i],
			    (unsigned long long)cap.value);
	}

	for (i = 0; i < 45; i++) {
		memset(&p, 0, sizeof(p));
		p.param = i;
		if ((e = xioctl(fd, DRM_IOCTL_PANFROST_GET_PARAM, &p)) != 0)
			printf("param %u: %s\n", i, ename(e));
		else
			printf("param %u: 0x%llx\n", i,
			    (unsigned long long)p.value);
	}
	memset(&p, 0, sizeof(p));
	p.pad = 1;
	if ((e = xioctl(fd, DRM_IOCTL_PANFROST_GET_PARAM, &p)) != 0)
		printf("param 0 with pad 1: %s\n", ename(e));
	else
		printf("param 0 with pad 1: 0x%llx\n",
		    (unsigned long long)p.value);

	/* Места буферов: та же последовательность, что у jobprobe.py. */
	create(fd, 0x4000, 0, &b[0]);
	create(fd, 0x1000, PANFROST_BO_NOEXEC, &b[1]);
	create(fd, 0x300000, PANFROST_BO_NOEXEC, &b[2]);
	create(fd, 0x19000, 0, &b[3]);
	create(fd, 0x100000, PANFROST_BO_NOEXEC | PANFROST_BO_HEAP, &b[4]);
	create(fd, 0, 0, NULL);
	create(fd, 0x1000, PANFROST_BO_HEAP, NULL);
	create(fd, 0x1000, 8, NULL);
	create(fd, 5000, PANFROST_BO_NOEXEC, &b[5]);
	for (i = 0; i < 2; i++) {
		memset(&off, 0, sizeof(off));
		off.handle = i ? 99 : b[3].handle;
		if ((e = xioctl(fd, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &off)))
			printf("get_bo_offset %u: %s\n", off.handle, ename(e));
		else
			printf("get_bo_offset %u: 0x%llx\n", off.handle,
			    (unsigned long long)off.offset);
	}
	{
		uint32_t mh[3] = { b[0].handle, b[4].handle, b[0].handle };
		uint32_t mf[3] = { 0, 0, 1 };

		for (i = 0; i < 3; i++) {
			memset(&mb, 0, sizeof(mb));
			mb.handle = mh[i];
			mb.flags = mf[i];
			if ((e = xioctl(fd, DRM_IOCTL_PANFROST_MMAP_BO, &mb)))
				printf("mmap_bo %u flags %u: %s\n", mh[i], mf[i],
				    ename(e));
			else
				printf("mmap_bo %u flags %u: token 0x%llx\n",
				    mh[i], mf[i],
				    (unsigned long long)mb.offset);
		}
	}
	for (i = 0; i < 2; i++) {
		memset(&gc, 0, sizeof(gc));
		gc.handle = b[1].handle;
		e = xioctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
		printf("gem_close %u%s: %s\n", b[1].handle, i ? " again" : "",
		    ename(e));
	}
	create(fd, 0x1000, PANFROST_BO_NOEXEC, &b[1]);
	{
		uint32_t mh[3] = { b[0].handle, b[0].handle, 77 };
		uint32_t mv[3] = { 1, 7, 0 };

		for (i = 0; i < 3; i++) {
			memset(&mad, 0, sizeof(mad));
			mad.handle = mh[i];
			mad.madv = mv[i];
			if ((e = xioctl(fd, DRM_IOCTL_PANFROST_MADVISE, &mad)))
				printf("madvise %u %u: %s\n", mh[i], mv[i],
				    ename(e));
			else
				printf("madvise %u %u: retained %u\n", mh[i],
				    mv[i], mad.retained);
		}
	}
	printf("wait_bo %u idle, deadline 0: %s\n", b[0].handle,
	    wait_bo(fd, b[0].handle, 0));
	printf("wait_bo 99: %s\n", wait_bo(fd, 99, 0));

	/* Объекты синхронизации и цепочка с ними. */
	so = sync_create(fd, 0);
	printf("syncobj create: handle %u\n", so);
	printf("syncobj wait %u (no fence): %s\n", so, sync_wait(fd, &so, 1, 0,
	    now_ns() + 1000000000LL, NULL));
	printf("syncobj wait %u for submit, 1 ms: %s\n", so, sync_wait(fd, &so,
	    1, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, now_ns() + 1000000LL,
	    NULL));
	h0 = b[0].handle;
	if ((e = map_bo(fd, &b[0])) != 0) {
		printf("map: %s\n", ename(e));
		return 1;
	}
	chain_into(&b[0], 64, 0x5a);
	e = submit(fd, b[0].va, &h0, 1, so, 0);
	{
		const char *wb = wait_bo(fd, h0, now_ns() + 2000000000LL);
		const char *sw = sync_wait(fd, &so, 1, 0, now_ns() +
		    1000000000LL, &first);

		printf("chain of 64 with out_sync %u: submit %s, wait_bo %s, "
		    "syncobj wait %s first %u, %u wrong word(s)\n", so,
		    ename(e), wb, sw, first, wrong(&b[0], 64, 0x5a));
	}
	printf("syncobj reset %u: %s; ", so, sync_array(fd,
	    DRM_IOCTL_SYNCOBJ_RESET, &so, 1));
	printf("wait: %s\n", sync_wait(fd, &so, 1, 0, now_ns(), NULL));
	printf("syncobj signal %u: %s; ", so, sync_array(fd,
	    DRM_IOCTL_SYNCOBJ_SIGNAL, &so, 1));
	printf("wait: %s\n", sync_wait(fd, &so, 1, 0, now_ns(), NULL));
	so2 = sync_create(fd, DRM_SYNCOBJ_CREATE_SIGNALED);
	hs[0] = so;
	hs[1] = so2;
	printf("syncobj create signaled: handle %u; wait all %u %u: %s\n", so2,
	    so, so2, sync_wait(fd, hs, 2, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
	    now_ns(), NULL));
	{
		struct drm_syncobj_destroy d;

		memset(&d, 0, sizeof(d));
		d.handle = so;
		e = xioctl(fd, DRM_IOCTL_SYNCOBJ_DESTROY, &d);
		printf("syncobj destroy %u: %s; ", so, ename(e));
		printf("wait: %s\n", sync_wait(fd, &so, 1, 0, now_ns(), NULL));
	}

	so = sync_create(fd, 0);
	printf("submit jc 0: %s\n", ename(submit(fd, 0, &h0, 1, 0, 0)));
	hs[0] = 99;
	printf("submit bo 99: %s\n", ename(submit(fd, b[0].va, hs, 1, 0, 0)));
	printf("submit bad array: %s\n", ename(submit(fd, b[0].va,
	    (uint32_t *)0x10, 1, 0, 0)));
	printf("submit out_sync 77: %s\n", ename(submit(fd, b[0].va, &h0, 1,
	    77, 0)));
	printf("submit requirements 4: %s\n", ename(submit(fd, b[0].va, &h0,
	    1, 0, 4)));
	(void)wait_bo(fd, h0, now_ns() + 2000000000LL);

	/* Долгая цепочка: занятый буфер со сроком в прошлом и со сроком 1 мс. */
	n = 32768;
	if (create(fd, 0x400000, 0, &bl) != 0 || (e = map_bo(fd, &bl)) != 0) {
		printf("long chain: no buffer\n");
		return 1;
	}
	hl = bl.handle;
	for (i = 0; i < n; i++)
		write_value_job(bl.m, JOB_SIZE * i, (i % 65535) + 1, i + 1 < n ?
		    bl.va + JOB_SIZE * (i + 1) : 0, bl.va + 0x3ff000 +
		    4 * (i % 1024), i);
	(void)submit(fd, bl.va, &hl, 1, 0, 0);
	{
		const char *a, *c, *d;

		a = wait_bo(fd, hl, 0);
		t = now_ns();
		c = wait_bo(fd, hl, t + 1000000LL);
		d = wait_bo(fd, hl, now_ns() + 5000000000LL);
		printf("long chain of %u: wait_bo deadline past: %s; deadline "
		    "+1 ms: %s; then: %s (%.1f ms)\n", n, a, c, d,
		    (double)(now_ns() - t) / 1e6);
	}

	/* Отказ задания: что видят WAIT_BO и ожидание объекта. */
	*(uint32_t *)(b[0].m + 0x3000) = 0xdeadbeef;
	*(uint32_t *)(b[0].m + 0x3004) = 0xdeadbeef;
	write_value_job(b[0].m, 0, 1, 0, b[0].va + API_FAULT_OFF,
	    FAULT_VALUE);
	e = submit(fd, b[0].va, &h0, 1, so, 0);
	{
		const char *wb = wait_bo(fd, h0, now_ns() + 2000000000LL);
		const char *sw = sync_wait(fd, &so, 1, 0, now_ns() +
		    2000000000LL, NULL);

		printf("fault at 0x%llx with out_sync %u: submit %s; wait_bo "
		    "%s; syncobj wait %s; header status 0x%x\n",
		    (unsigned long long)(b[0].va + API_FAULT_OFF), so,
		    ename(e), wb, sw, *(uint32_t *)b[0].m);
	}
	chain_into(&b[0], 64, 0x6b);
	(void)submit(fd, b[0].va, &h0, 1, so, 0);
	(void)wait_bo(fd, h0, now_ns() + 2000000000LL);
	printf("chain after the fault: %u wrong word(s)\n", wrong(&b[0], 64,
	    0x6b));

	/*
	 * Куча: задание пишет в неё, первые 2 МиБ и следующие.  Через
	 * секунду, как у jobprobe.py: там panfrost после отказа сбрасывает
	 * ускоритель по таймауту, и куча проверяется не на этом.
	 */
	sleep(1);
	if (create(fd, 0x400000, PANFROST_BO_NOEXEC | PANFROST_BO_HEAP, &bh) ==
	    0) {
		for (i = 0; i < 2; i++) {
			const char *wb, *sw;

			at = i ? 0x300000 : 0x1000;
			hs[0] = h0;
			hs[1] = bh.handle;
			write_value_job(b[0].m, 0, 1, 0, bh.va + at, 0x12345678);
			e = submit(fd, b[0].va, hs, 2, so, 0);
			wb = wait_bo(fd, h0, now_ns() + 2000000000LL);
			sw = sync_wait(fd, &so, 1, 0, now_ns() + 2000000000LL,
			    NULL);
			printf("heap write at +0x%x: submit %s, wait_bo %s, "
			    "syncobj %s, header status 0x%x\n", at, ename(e), wb,
			    sw, *(uint32_t *)b[0].m);
		}
	}
	close(fd);
	return 0;
}

/* ---- two ---------------------------------------------------------------- */

static void
client(const char *tag, unsigned rounds, int fault_at, int out)
{
	char lines[1024];
	struct bo b;
	uint32_t h, so;
	unsigned k, bad = 0, chains = 0, t = tag[0] == 'A' ? 0xa0 : 0xb0;
	size_t len;
	int fd, e;

	fd = open_node();
	memset(&b, 0, sizeof(b));
	{
		struct drm_panfrost_create_bo c;

		memset(&c, 0, sizeof(c));
		c.size = 0x4000;
		if ((e = xioctl(fd, DRM_IOCTL_PANFROST_CREATE_BO, &c)) != 0) {
			dprintf(out, "client %s: create: %s\n", tag, ename(e));
			return;
		}
		b.handle = c.handle;
		b.va = c.offset;
		b.size = 0x4000;
	}
	if ((e = map_bo(fd, &b)) != 0) {
		dprintf(out, "client %s: map: %s\n", tag, ename(e));
		return;
	}
	h = b.handle;
	so = sync_create(fd, 0);
	len = (size_t)snprintf(lines, sizeof(lines), "client %s: va 0x%llx\n",
	    tag, (unsigned long long)b.va);
	for (k = 0; k < rounds; k++) {
		if ((int)k == fault_at) {
			const char *wb, *sw;

			write_value_job(b.m, 0, 1, 0, b.va + FAULT_OFF,
			    FAULT_VALUE);
			(void)submit(fd, b.va, &h, 1, so, 0);
			wb = wait_bo(fd, h, now_ns() + 2000000000LL);
			sw = sync_wait(fd, &so, 1, 0, now_ns() + 2000000000LL,
			    NULL);
			len += (size_t)snprintf(lines + len, sizeof(lines) - len,
			    "client %s: fault at round %u: wait_bo %s, syncobj "
			    "wait %s\n", tag, k, wb, sw);
			continue;
		}
		chain_into(&b, 64, t);
		e = submit(fd, b.va, &h, 1, so, 0);
		chains++;
		if (e != 0 || strcmp(sync_wait(fd, &so, 1, 0, now_ns() +
		    2000000000LL, NULL), "ok") != 0 || wrong(&b, 64, t) != 0)
			bad++;
	}
	len += (size_t)snprintf(lines + len, sizeof(lines) - len, "client %s: "
	    "%u chains of 64, %u bad\n", tag, chains, bad);
	(void)write(out, lines, len);
	close(fd);
}

static int
two(void)
{
	int p[2][2], i, n;
	pid_t pid[2];
	char buf[2048];

	for (i = 0; i < 2; i++) {
		if (pipe(p[i]) != 0)
			return 1;
		if ((pid[i] = fork()) == 0) {
			close(p[i][0]);
			client(i ? "B" : "A", 200, i ? -1 : 50, p[i][1]);
			_exit(0);
		}
		close(p[i][1]);
	}
	for (i = 0; i < 2; i++) {
		(void)waitpid(pid[i], NULL, 0);
		while ((n = (int)read(p[i][0], buf, sizeof(buf) - 1)) > 0) {
			buf[n] = '\0';
			fputs(buf, stdout);
		}
	}
	return 0;
}

/* ---- heap --------------------------------------------------------------- */

#define HEAP_SIZE	0x4000000u	/* 64 MiB: 32 куска */
#define PIECE		0x200000u

/* Цепочка из n WRITE_VALUE: задание i пишет tag<<24|i по адресу at[i]. */
static void
chain_to(struct bo *b, unsigned n, const uint64_t *at, unsigned tag)
{
	unsigned i;

	for (i = 0; i < n; i++)
		write_value_job(b->m, JOB_SIZE * i, i + 1, i + 1 < n ?
		    b->va + JOB_SIZE * (i + 1) : 0, at[i],
		    (uint32_t)tag << 24 | i);
}

/* Сколько заголовков цепочки из n говорят «сделано» (1). */
static unsigned
headers_done(struct bo *b, unsigned n)
{
	unsigned i, k = 0;

	for (i = 0; i < n; i++)
		k += *(uint32_t *)(b->m + JOB_SIZE * i) == 1;
	return k;
}

static int
heap(void)
{
	struct bo b, h;
	uint64_t at[16];
	uint32_t hs[2], so;
	int64_t t;
	unsigned i;
	int fd, e;

	fd = open_node();
	printf("render node %s, mode heap\n", NODE);
	if (create(fd, 0x4000, 0, &b) != 0 || (e = map_bo(fd, &b)) != 0 ||
	    create(fd, HEAP_SIZE, PANFROST_BO_NOEXEC | PANFROST_BO_HEAP,
	    &h) != 0)
		return 1;
	hs[0] = b.handle;
	hs[1] = h.handle;
	so = sync_create(fd, 0);

	/* Цепочка до кучи (зонд берёт из неё номер адресного пространства). */
	chain_into(&b, 64, 0x5a);
	e = submit(fd, b.va, hs, 1, so, 0);
	(void)sync_wait(fd, &so, 1, 0, now_ns() + 2000000000LL, NULL);
	printf("chain before: submit %s, %u wrong word(s)\n", ename(e),
	    wrong(&b, 64, 0x5a));

	/* Первый кусок, тот же кусок, последний кусок - по заданию. */
	{
		static const uint32_t offs[] = { 0x1000, 0x1ff040,
		    HEAP_SIZE - 0x40 };
		static const char *const what[] = { "", " (same piece)",
		    " (last piece)" };

		for (i = 0; i < 3; i++) {
			at[0] = h.va + offs[i];
			chain_to(&b, 1, at, 0x40 + i);
			e = submit(fd, b.va, hs, 2, so, 0);
			printf("heap write at +0x%x%s: submit %s, syncobj %s, "
			    "header status 0x%x\n", offs[i], what[i], ename(e),
			    sync_wait(fd, &so, 1, 0, now_ns() + 2000000000LL,
			    NULL), *(uint32_t *)b.m);
		}
	}

	/* Одно задание через 16 новых кусков (с 1 по 16), каждый - отказ. */
	for (i = 0; i < 16; i++)
		at[i] = h.va + (i + 1) * (uint64_t)PIECE + 0x40 * (i + 1);
	chain_to(&b, 16, at, 0x50);
	t = now_ns();
	e = submit(fd, b.va, hs, 2, so, 0);
	{
		const char *sw = sync_wait(fd, &so, 1, 0, now_ns() +
		    2000000000LL, NULL);

		printf("chain of 16 through pieces 1-16: submit %s, syncobj %s, "
		    "%u of 16 header(s) done\n", ename(e), sw, headers_done(&b,
		    16));
		printf("(16 pieces in %.1f ms)\n", (double)(now_ns() - t) / 1e6);
	}

	/*
	 * За концом кучи: отказ, которого кучей не решить.  Слово состояния
	 * заголовка - ноль, как его оставляет chain_to(): с ненулевым
	 * ускоритель отвергает дескриптор сам (0x58, DATA_INVALID_FAULT),
	 * не дойдя до записи, - так первая версия этой проверки и вышла.
	 */
	at[0] = h.va + HEAP_SIZE + 0x40;
	chain_to(&b, 1, at, 0x60);
	e = submit(fd, b.va, hs, 2, so, 0);
	printf("write past the heap (+0x%x): submit %s, wait_bo %s, syncobj "
	    "%s, header status 0x%x\n", HEAP_SIZE + 0x40, ename(e),
	    wait_bo(fd, b.handle, now_ns() + 2000000000LL), sync_wait(fd, &so,
	    1, 0, now_ns() + 2000000000LL, NULL), *(uint32_t *)b.m);

	/* Жив ли: цепочка в b после отказа (у panfrost - через секунду). */
	sleep(1);
	chain_into(&b, 64, 0x6b);
	e = submit(fd, b.va, hs, 2, so, 0);
	(void)sync_wait(fd, &so, 1, 0, now_ns() + 2000000000LL, NULL);
	printf("chain after: submit %s, %u wrong word(s)\n", ename(e),
	    wrong(&b, 64, 0x6b));
	close(fd);
	return 0;
}

/* ---- cost --------------------------------------------------------------- */

/* n цепочек из 64 в b с буферами hs: миллисекунд на цепочку, плохих в bad. */
static double
chains(int fd, struct bo *b, uint32_t *hs, unsigned nh, uint32_t so,
	unsigned n, unsigned *bad)
{
	int64_t t0 = now_ns();
	unsigned k;

	*bad = 0;
	for (k = 0; k < n; k++) {
		chain_into(b, 64, 0x5a);
		if (submit(fd, b->va, hs, nh, so, 0) != 0 ||
		    strcmp(sync_wait(fd, &so, 1, 0, now_ns() + 2000000000LL,
		    NULL), "ok") != 0 || wrong(b, 64, 0x5a) != 0)
			(*bad)++;
	}
	return (double)(now_ns() - t0) / 1e6 / n;
}

/*
 * Цена обслуживания кэша: служба чистит каждый буфер задания, который
 * отображал процессор, целиком до задания и после.  Цепочка та же, в
 * списке задания - ещё один буфер размера S: не отображённый (его не
 * чистят) и отображённый (чистят).  Разница - цена S.
 */
static int
cost(unsigned n)
{
	static const uint32_t sizes[] = { 0x4000, 0x100000, 0x400000,
	    0x1000000, 0x4000000 };
	struct bo b, big;
	struct drm_gem_close gc;
	uint32_t hs[2], so;
	double plain, cold, mapped;
	unsigned i, bad1, bad2, bad3;
	int fd;

	fd = open_node();
	if (create(fd, 0x4000, 0, &b) != 0 || map_bo(fd, &b) != 0)
		return 1;
	hs[0] = b.handle;
	so = sync_create(fd, 0);
	plain = chains(fd, &b, hs, 1, so, n, &bad1);
	printf("cost: %u chain(s) of 64, alone: %.3f ms each, %u bad\n", n,
	    plain, bad1);
	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		/*
		 * Часы MEECHO идут тиком (10 мс): мелкое меряется числом
		 * повторов; крупное - десятой их частью, чтобы не держать
		 * процессор чисткой минутами.
		 */
		unsigned k = sizes[i] >= 0x1000000 && n >= 100 ? n / 10 : n;

		if (create(fd, sizes[i], PANFROST_BO_NOEXEC, &big) != 0)
			break;
		hs[1] = big.handle;
		cold = chains(fd, &b, hs, 2, so, k, &bad2);
		if (map_bo(fd, &big) != 0) {
			printf("cost: no map of 0x%x\n", sizes[i]);
			break;
		}
		big.m[0] = 1;		/* процессор его коснулся */
		mapped = chains(fd, &b, hs, 2, so, k, &bad3);
		printf("cost: + %5u KiB buffer, %u chains: %.3f ms unmapped, "
		    "%.3f ms mapped (+%.3f ms, %.2f ms per MiB), %u bad\n",
		    sizes[i] / 1024, k, cold, mapped, mapped - cold,
		    (mapped - cold) * 1048576.0 / sizes[i], bad2 + bad3);
		munmap(big.m, big.size);
		memset(&gc, 0, sizeof(gc));
		gc.handle = big.handle;
		(void)xioctl(fd, DRM_IOCTL_GEM_CLOSE, &gc);
	}
	close(fd);
	return 0;
}

/* ---- chain -------------------------------------------------------------- */

static int
chain(unsigned times)
{
	struct bo b;
	uint32_t h, so;
	unsigned k, bad = 0;
	int64_t t0;
	int fd, e;

	fd = open_node();
	if (create(fd, 0x4000, 0, &b) != 0 || (e = map_bo(fd, &b)) != 0)
		return 1;
	h = b.handle;
	so = sync_create(fd, 0);
	t0 = now_ns();
	for (k = 0; k < times; k++) {
		chain_into(&b, 64, 0x5a);
		if ((e = submit(fd, b.va, &h, 1, so, 0)) != 0) {
			printf("submit: %s\n", ename(e));
			return 1;
		}
		if (strcmp(sync_wait(fd, &so, 1, 0, now_ns() + 2000000000LL,
		    NULL), "ok") != 0 || wrong(&b, 64, 0x5a) != 0)
			bad++;
	}
	printf("%u chain(s) of 64 WRITE_VALUE: %u bad, %.3f ms each\n", times,
	    bad, (double)(now_ns() - t0) / 1e6 / times);
	close(fd);
	return bad != 0;
}

/* ---- prime -------------------------------------------------------------- */

/*
 * Буферы и заборы дескрипторами (веха 11.6, вторая часть): то же, что
 * jobprobe.py prime под panfrost, строка в строку (ref-prime.txt против
 * meecho-prime.txt).  A и B - два открытия узла в одном процессе, C -
 * ребёнок, получающий буфер через сокет (SCM_RIGHTS).  Номера
 * дескрипторов не печатаются.
 */

#define LONG_N		65000u
#define LONG_WORD	(0x3ff000u + 4 * ((LONG_N - 1) % 1024))
#define PRIME_FAULT_VA	0x10000040ull
#define SYNC_FILE_FLAG	1u

static const char *
handle_or(int e, uint32_t h)
{
	static char buf[4][32];
	static unsigned k;
	char *b = buf[k++ % 4];

	if (e != 0)
		return ename(e);
	snprintf(b, sizeof(buf[0]), "handle %u", h);
	return b;
}

static int
prime_export(int fd, uint32_t h, uint32_t flags, int *out)
{
	struct drm_prime_handle a;
	int e;

	memset(&a, 0, sizeof(a));
	a.handle = h;
	a.flags = flags;
	a.fd = -1;
	e = xioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &a);
	*out = e == 0 ? a.fd : -1;
	return e;
}

static int
prime_import(int fd, int dfd, uint32_t *h)
{
	struct drm_prime_handle a;
	int e;

	memset(&a, 0, sizeof(a));
	a.fd = dfd;
	e = xioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &a);
	*h = a.handle;
	return e;
}

static int
so_export(int fd, uint32_t so, uint32_t flags, int *out)
{
	struct drm_syncobj_handle a;
	int e;

	memset(&a, 0, sizeof(a));
	a.handle = so;
	a.flags = flags;
	a.fd = -1;
	e = xioctl(fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &a);
	*out = e == 0 ? a.fd : -1;
	return e;
}

static int
so_import(int fd, int sfd, uint32_t flags, uint32_t *so)
{
	struct drm_syncobj_handle a;
	int e;

	memset(&a, 0, sizeof(a));
	a.handle = *so;
	a.flags = flags;
	a.fd = sfd;
	e = xioctl(fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &a);
	*so = a.handle;
	return e;
}

static const char *
readiness(int sfd, int ms)
{
	struct pollfd p;

	p.fd = sfd;
	p.events = POLLIN;
	p.revents = 0;
	return poll(&p, 1, ms) == 1 && (p.revents & POLLIN) ? "ready" :
	    "not ready";
}

static const char *
file_info(int sfd)
{
	static char buf[4][48];
	static unsigned k;
	struct sync_file_info i;
	char *b = buf[k++ % 4];
	int e;

	memset(&i, 0, sizeof(i));
	if ((e = xioctl(sfd, SYNC_IOC_FILE_INFO, &i)) != 0)
		return ename(e);
	snprintf(b, sizeof(buf[0]), "status %d, %u fence(s)", i.status,
	    i.num_fences);
	return b;
}

static int
merge(int f1, int f2, int *out)
{
	struct sync_merge_data m;
	int e;

	memset(&m, 0, sizeof(m));
	strlcpy(m.name, "merged", sizeof(m.name));
	m.fd2 = f2;
	m.fence = -1;
	e = xioctl(f1, SYNC_IOC_MERGE, &m);
	*out = e == 0 ? m.fence : -1;
	return e;
}

static int
cloexec(int f)
{
	return (fcntl(f, F_GETFD) & FD_CLOEXEC) != 0;
}

static void
long_chain(struct bo *b)
{
	unsigned i;

	for (i = 0; i < LONG_N; i++)
		write_value_job(b->m, JOB_SIZE * i, (i % 65535) + 1, i + 1 <
		    LONG_N ? b->va + JOB_SIZE * (i + 1) : 0, b->va + 0x3ff000 +
		    4 * (i % 1024), i);
}

static const char *
offset_of(int fd, uint32_t h, uint64_t *va)
{
	static char buf[4][24];
	static unsigned k;
	struct drm_panfrost_get_bo_offset o;
	char *b = buf[k++ % 4];
	int e;

	memset(&o, 0, sizeof(o));
	o.handle = h;
	if ((e = xioctl(fd, DRM_IOCTL_PANFROST_GET_BO_OFFSET, &o)) != 0)
		return ename(e);
	if (va != NULL)
		*va = o.offset;
	snprintf(b, sizeof(buf[0]), "0x%llx", (unsigned long long)o.offset);
	return b;
}

static const char *
pattern_in(const unsigned char *m)
{
	unsigned i;

	for (i = 0; i < 256; i++)
		if (m[0x2000 + i] != i)
			return "differs";
	return "equal";
}

static unsigned
wrong_at(const unsigned char *m, unsigned n, unsigned tag)
{
	unsigned i, w = 0;

	for (i = 0; i < n; i++)
		w += ((const uint32_t *)(m + 0x1000))[i] != ((uint32_t)tag << 24 |
		    i);
	return w;
}

/* Цепочка из 64 в code, пишущая tag<<24|i по адресу va + 0x1000 + 4i. */
static void
chain_to_va(struct bo *code, uint64_t va, unsigned tag)
{
	unsigned i;

	for (i = 0; i < 64; i++)
		write_value_job(code->m, JOB_SIZE * i, i + 1, i + 1 < 64 ?
		    code->va + JOB_SIZE * (i + 1) : 0, va + 0x1000 + 4 * i,
		    (uint32_t)tag << 24 | i);
}

static int
map_import(int fd, uint32_t h, struct bo *b)
{
	memset(b, 0, sizeof(*b));
	b->handle = h;
	b->size = 0x4000;
	(void)offset_of(fd, h, &b->va);
	return map_bo(fd, b);
}

/* Ребёнок C: буфер через сокет, свой контекст, запись GPU. */
static void
prime_child(int sock)
{
	char line[512], c;
	struct msghdr msg;
	struct cmsghdr *cm;
	union {
		struct cmsghdr h;
		char buf[CMSG_SPACE(sizeof(int))];
	} cbuf;
	struct iovec iov;
	struct bo code, imp;
	uint32_t h = 0, hs[2], so;
	off_t size;
	int d = -1, fc, e, e2;

	memset(&msg, 0, sizeof(msg));
	iov.iov_base = &c;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cbuf.buf;
	msg.msg_controllen = sizeof(cbuf.buf);
	if (recvmsg(sock, &msg, 0) == 1 && (cm = CMSG_FIRSTHDR(&msg)) != NULL)
		memcpy(&d, CMSG_DATA(cm), sizeof(d));
	fc = open_node();
	e = prime_import(fc, d, &h);
	size = lseek(d, 0, SEEK_END);
	{
		struct drm_panfrost_create_bo cb;

		memset(&cb, 0, sizeof(cb));
		cb.size = 0x4000;
		(void)xioctl(fc, DRM_IOCTL_PANFROST_CREATE_BO, &cb);
		memset(&code, 0, sizeof(code));
		code.handle = cb.handle;
		code.va = cb.offset;
		code.size = 0x4000;
		(void)map_bo(fc, &code);
	}
	(void)map_import(fc, h, &imp);
	chain_to_va(&code, imp.va, 0xc1);
	so = sync_create(fc, 0);
	hs[0] = code.handle;
	hs[1] = h;
	e2 = submit(fc, code.va, hs, 2, so, 0);
	snprintf(line, sizeof(line), "child: import %s, size 0x%llx, address "
	    "0x%llx, the pattern %s; its GPU writes 64 words: submit %s, wait "
	    "%s", handle_or(e, h), (unsigned long long)size,
	    (unsigned long long)imp.va, pattern_in(imp.m), ename(e2),
	    sync_wait(fc, &so, 1, 0, now_ns() + 2000000000LL, NULL));
	(void)write(sock, line, strlen(line));
	close(fc);
}

static int
prime(void)
{
	struct bo a, l, hp, bb, cb, bl, imp, m5b;
	struct drm_get_cap cap;
	struct drm_gem_close gc;
	unsigned char *md;
	uint32_t h, hi, h5, hli, so, sob, soa, sin, sout, snap, sig, x, y, z;
	uint32_t hs[2], sin2;
	uint64_t vai, vali;
	int fa, fb, p[2], d1, d2, d3, d4, dl, sf, s2, fsig, frun, fm, fo, ff;
	int e, e2, e3, sv[2], i;
	pid_t pid;

	fa = open_node();
	fb = open_node();
	if (pipe(p) != 0)
		return 1;
	printf("render node %s, mode prime\n", NODE);
	memset(&cap, 0, sizeof(cap));
	cap.capability = DRM_CAP_PRIME;
	if ((e = xioctl(fa, DRM_IOCTL_GET_CAP, &cap)) != 0)
		printf("cap prime: %s\n", ename(e));
	else
		printf("cap prime: %llu\n", (unsigned long long)cap.value);

	if (create(fa, 0x4000, 0, &a) != 0 || create(fa, 0x400000, 0, &l) ||
	    create(fa, 0x200000, PANFROST_BO_NOEXEC | PANFROST_BO_HEAP, &hp) ||
	    create(fb, 0x100000, PANFROST_BO_NOEXEC, &bb) ||
	    create(fb, 0x4000, 0, &cb) || create(fb, 0x400000, 0, &bl))
		return 1;
	if (map_bo(fa, &a) || map_bo(fa, &l) || map_bo(fb, &cb) ||
	    map_bo(fb, &bl))
		return 1;
	for (i = 0; i < 256; i++)
		a.m[0x2000 + i] = (unsigned char)i;

	/* Вывоз: дескриптор, его флаги, размер через lseek. */
	e = prime_export(fa, a.handle, O_CLOEXEC | O_RDWR, &d1);
	{
		off_t end = lseek(d1, 0, SEEK_END), set = lseek(d1, 0, SEEK_SET);

		printf("export %u: %s; close-on-exec %d, lseek to the end 0x%llx, "
		    "back to %lld\n", a.handle, ename(e), cloexec(d1),
		    (unsigned long long)end, (long long)set);
	}
	e = prime_export(fa, a.handle, 0, &d2);
	printf("export %u again, no flags: %s; close-on-exec %d\n", a.handle,
	    ename(e), cloexec(d2));
	printf("export 99: %s\n", ename(prime_export(fa, 99, O_CLOEXEC |
	    O_RDWR, &d3)));
	printf("export %u, flags 0x1234: %s\n", a.handle, ename(prime_export(fa,
	    a.handle, 0x1234, &d3)));
	e = prime_export(fa, hp.handle, O_CLOEXEC | O_RDWR, &d3);
	printf("export the heap %u: %s\n", hp.handle, ename(e));
	if (d3 >= 0)
		close(d3);

	/* Ввоз: в тот же контекст, в другой, вторая выдача, не dma-buf. */
	e = prime_import(fa, d1, &h);
	printf("import into the exporter: %s\n", handle_or(e, h));
	e = prime_import(fb, d1, &hi);
	printf("import into B: %s, address %s (A's 0x%llx)\n", handle_or(e,
	    hi), offset_of(fb, hi, &vai), (unsigned long long)a.va);
	e = prime_import(fb, d2, &h);
	printf("import the second export into B: %s\n", handle_or(e, h));
	printf("import -1: %s\n", ename(prime_import(fb, -1, &h)));
	printf("import a pipe: %s\n", ename(prime_import(fb, p[0], &h)));
	printf("import the render node: %s\n", ename(prime_import(fb, fa, &h)));

	/* Память одна: отображение импорта и mmap(2) самого дескриптора. */
	(void)map_import(fb, hi, &imp);
	printf("B's mapping of the import: the pattern %s\n", pattern_in(imp.m));
	md = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE, MAP_SHARED, d1, 0);
	if (md == MAP_FAILED) {
		printf("mmap of the descriptor: %s\n", ename(errno));
	} else {
		const char *eq = pattern_in(md);

		md[0x2100] = 0xa5;
		md[0x2101] = 0x5a;
		md[0x2102] = 0xc3;
		md[0x2103] = 0x3c;
		printf("mmap of the descriptor: the pattern %s, a write through "
		    "it %s to A\n", eq, memcmp(a.m + 0x2100, "\xa5\x5a\xc3\x3c",
		    4) == 0 ? "visible" : "NOT visible");
		munmap(md, 0x4000);
	}

	/* GPU: B пишет в импорт, A - в свой буфер. */
	memset(a.m + 0x1000, 0, 0x100);
	chain_to_va(&cb, vai, 0xb1);
	sob = sync_create(fb, 0);
	hs[0] = cb.handle;
	hs[1] = hi;
	e = submit(fb, cb.va, hs, 2, sob, 0);
	printf("B's GPU writes 64 words into the import: submit %s, wait %s; A "
	    "sees %u wrong\n", ename(e), sync_wait(fb, &sob, 1, 0, now_ns() +
	    2000000000LL, NULL), wrong_at(a.m, 64, 0xb1));
	chain_into(&a, 64, 0xa1);
	soa = sync_create(fa, 0);
	e = submit(fa, a.va, &a.handle, 1, soa, 0);
	printf("A's GPU writes 64 words into it: submit %s, wait %s; B sees %u "
	    "wrong\n", ename(e), sync_wait(fa, &soa, 1, 0, now_ns() +
	    2000000000LL, NULL), wrong_at(imp.m, 64, 0xa1));

	/* Неявная синхронизация: долгое задание B называет общий буфер. */
	long_chain(&bl);
	hs[0] = bl.handle;
	hs[1] = hi;
	e = submit(fb, bl.va, hs, 2, 0, 0);
	{
		const char *w1 = wait_bo(fa, a.handle, 0);
		const char *w2 = wait_bo(fa, a.handle, now_ns() + 5000000000LL);

		printf("B's long chain names the shared buffer: submit %s; A's "
		    "wait_bo, deadline past: %s; then: %s\n", ename(e), w1, w2);
	}

	/* Жизнь буфера: A закрывает номер и дескрипторы - буфер жив у B. */
	memset(&gc, 0, sizeof(gc));
	gc.handle = a.handle;
	e = xioctl(fa, DRM_IOCTL_GEM_CLOSE, &gc);
	close(d1);
	close(d2);
	munmap(a.m, 0x4000);
	printf("A closes handle %u and both descriptors: %s; B's mapping: the "
	    "pattern %s\n", a.handle, ename(e), pattern_in(imp.m));
	e = prime_export(fb, hi, O_CLOEXEC | O_RDWR, &d4);
	printf("B exports its import: %s\n", ename(e));
	e = prime_import(fa, d4, &h5);
	printf("A imports it back: %s, address %s\n", handle_or(e, h5),
	    offset_of(fa, h5, NULL));
	(void)map_import(fa, h5, &m5b);
	printf("A's new mapping: the pattern %s\n", pattern_in(m5b.m));

	/* Другой процесс: дескриптор через сокет (SCM_RIGHTS). */
	memset(m5b.m + 0x1000, 0, 0x100);
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
		return 1;
	if ((pid = fork()) == 0) {
		close(sv[0]);
		prime_child(sv[1]);
		_exit(0);
	}
	close(sv[1]);
	{
		struct msghdr msg;
		struct cmsghdr *cm;
		union {
			struct cmsghdr h;
			char buf[CMSG_SPACE(sizeof(int))];
		} cbuf;
		struct iovec iov;
		char c = 'x', line[512];
		ssize_t n;

		memset(&msg, 0, sizeof(msg));
		iov.iov_base = &c;
		iov.iov_len = 1;
		msg.msg_iov = &iov;
		msg.msg_iovlen = 1;
		msg.msg_control = cbuf.buf;
		msg.msg_controllen = CMSG_LEN(sizeof(int));
		cm = CMSG_FIRSTHDR(&msg);
		cm->cmsg_level = SOL_SOCKET;
		cm->cmsg_type = SCM_RIGHTS;
		cm->cmsg_len = CMSG_LEN(sizeof(int));
		memcpy(CMSG_DATA(cm), &d4, sizeof(d4));
		(void)sendmsg(sv[0], &msg, 0);
		n = read(sv[0], line, sizeof(line) - 1);
		line[n > 0 ? n : 0] = '\0';
		(void)waitpid(pid, NULL, 0);
		close(sv[0]);
		printf("%s\n", line);
	}
	printf("A sees the child's words: %u wrong\n", wrong_at(m5b.m, 64,
	    0xc1));

	/* Sync file: без забора; долгое задание A; B после него. */
	so = sync_create(fa, 0);
	printf("sync file of a sync object without a fence: %s\n",
	    ename(so_export(fa, so, SYNC_FILE_FLAG, &sf)));
	(void)prime_export(fa, l.handle, O_CLOEXEC | O_RDWR, &dl);
	(void)prime_import(fb, dl, &hli);
	(void)offset_of(fb, hli, &vali);
	long_chain(&l);
	e = submit(fa, l.va, &l.handle, 1, so, 0);
	e3 = so_export(fa, so, SYNC_FILE_FLAG, &sf);
	{
		const char *r0 = readiness(sf, 0), *i0 = file_info(sf);

		printf("long chain in A: submit %s, sync file %s; at once: %s, "
		    "%s\n", ename(e), ename(e3), r0, i0);
	}
	sin = sync_create(fb, 0);
	printf("B imports the sync file into sync object %u: %s\n", sin,
	    ename(so_import(fb, sf, SYNC_FILE_FLAG, &sin)));
	write_value_job(cb.m, 0, 1, 0, vali + LONG_WORD, 0xb0b0b0b0u);
	sout = sync_create(fb, 0);
	hs[0] = cb.handle;
	hs[1] = hli;
	{
		struct drm_panfrost_submit s;

		memset(&s, 0, sizeof(s));
		s.jc = cb.va;
		s.bo_handles = (uint64_t)(uintptr_t)hs;
		s.bo_handle_count = 2;
		s.in_syncs = (uint64_t)(uintptr_t)&sin;
		s.in_sync_count = 1;
		s.out_sync = sout;
		e = xioctl(fb, DRM_IOCTL_PANFROST_SUBMIT, &s);
	}
	printf("B's job after it: submit %s; its sync object at once: %s\n",
	    ename(e), sync_wait(fb, &sout, 1, 0, now_ns(), NULL));
	{
		const char *r5 = readiness(sf, 5000), *i5 = file_info(sf);

		printf("the sync file within 5 s: %s, %s\n", r5, i5);
	}
	{
		const char *sw = sync_wait(fb, &sout, 1, 0, now_ns() +
		    2000000000LL, NULL);

		printf("B's job: %s; the word both write: 0x%08x (B's 0xb0b0b0b0, "
		    "A's 0x%x)\n", sw, *(uint32_t *)(l.m + LONG_WORD), LONG_N - 1);
	}

	/* Забор Mesa: panfrost_fence_create(). */
	snap = sync_create(fa, 0);
	e = so_export(fa, so, SYNC_FILE_FLAG, &s2);
	e2 = so_import(fa, s2, SYNC_FILE_FLAG, &snap);
	close(s2);
	printf("Mesa's fence: export %s, import %s, wait %s\n", ename(e),
	    ename(e2), sync_wait(fa, &snap, 1, 0, now_ns() + 1000000000LL,
	    NULL));

	/* Слияние. */
	sig = sync_create(fa, DRM_SYNCOBJ_CREATE_SIGNALED);
	e = so_export(fa, sig, SYNC_FILE_FLAG, &fsig);
	{
		const char *r0 = readiness(fsig, 0), *i0 = file_info(fsig);

		printf("sync file of a signaled sync object: %s, %s, %s\n",
		    ename(e), r0, i0);
	}
	long_chain(&l);
	(void)submit(fa, l.va, &l.handle, 1, so, 0);
	(void)so_export(fa, so, SYNC_FILE_FLAG, &frun);
	e = merge(fsig, frun, &fm);
	{
		const char *r0 = readiness(fm, 0), *i0 = file_info(fm);

		printf("merged with a running one: %s; at once: %s, %s\n",
		    ename(e), r0, i0);
	}
	{
		const char *r5 = readiness(fm, 5000), *i5 = file_info(fm);

		printf("  within 5 s: %s, %s\n", r5, i5);
	}
	printf("merge with a pipe: %s\n", ename(merge(fsig, p[0], &i)));
	close(fm);
	close(frun);

	printf("import a pipe as a sync file: %s\n", ename(so_import(fa, p[0],
	    SYNC_FILE_FLAG, &snap)));
	printf("import a dma-buf as a sync file: %s\n", ename(so_import(fa, d4,
	    SYNC_FILE_FLAG, &snap)));
	printf("import -1 as a sync file: %s\n", ename(so_import(fa, -1,
	    SYNC_FILE_FLAG, &snap)));
	h = 99;
	printf("import a sync file into sync object 99: %s\n",
	    ename(so_import(fa, fsig, SYNC_FILE_FLAG, &h)));
	printf("export sync object 99 as a sync file: %s\n", ename(so_export(fa,
	    99, SYNC_FILE_FLAG, &i)));
	printf("export with flags 2: %s\n", ename(so_export(fa, so, 2, &i)));

	/* Объект синхронизации целиком: один в двух контекстах. */
	x = sync_create(fa, 0);
	e = so_export(fa, x, 0, &fo);
	y = 0;
	e2 = so_import(fb, fo, 0, &y);
	printf("sync object %u as a descriptor: %s; into B: %s\n", x, ename(e),
	    handle_or(e2, y));
	{
		const char *s1 = sync_array(fa, DRM_IOCTL_SYNCOBJ_SIGNAL, &x, 1);

		printf("A signals it: %s; B's wait: %s\n", s1, sync_wait(fb, &y,
		    1, 0, now_ns(), NULL));
	}
	{
		const char *s1 = sync_array(fb, DRM_IOCTL_SYNCOBJ_RESET, &y, 1);

		printf("B resets it: %s; A's wait: %s\n", s1, sync_wait(fa, &x, 1,
		    0, now_ns(), NULL));
	}
	z = 0;
	e = so_import(fa, fo, 0, &z);
	printf("into A itself: %s\n", handle_or(e, z));
	h = 0;
	printf("a sync file as a sync object: %s\n", ename(so_import(fa, fsig,
	    0, &h)));
	h = 0;
	printf("a dma-buf as a sync object: %s\n", ename(so_import(fa, d4, 0,
	    &h)));
	{
		struct drm_syncobj_transfer t;

		memset(&t, 0, sizeof(t));
		t.src_handle = x;
		t.dst_handle = snap;
		printf("transfer: %s\n", ename(xioctl(fa,
		    DRM_IOCTL_SYNCOBJ_TRANSFER, &t)));
	}
	close(fo);

	/* Ioctl самого dma-buf. */
	{
		static const uint64_t fl[] = { DMA_BUF_SYNC_READ,
		    DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ, DMA_BUF_SYNC_RW,
		    DMA_BUF_SYNC_END | DMA_BUF_SYNC_RW, 0, 8 };
		struct dma_buf_sync ds;
		unsigned k;

		for (k = 0; k < sizeof(fl) / sizeof(fl[0]); k++) {
			ds.flags = fl[k];
			printf("dma-buf sync 0x%llx: %s\n",
			    (unsigned long long)fl[k], ename(xioctl(dl,
			    DMA_BUF_IOCTL_SYNC, &ds)));
		}
	}
	long_chain(&l);
	(void)submit(fa, l.va, &l.handle, 1, 0, 0);
	{
		struct dma_buf_export_sync_file ex;

		memset(&ex, 0, sizeof(ex));
		ex.flags = DMA_BUF_SYNC_READ;
		ex.fd = -1;
		if ((e = xioctl(dl, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex)) == 0) {
			const char *r0 = readiness(ex.fd, 0), *i0 =
			    file_info(ex.fd), *r5 = readiness(ex.fd, 5000);

			printf("dma-buf export sync file of the busy buffer: ok; "
			    "at once: %s, %s; within 5 s: %s\n", r0, i0, r5);
			close(ex.fd);
		} else
			printf("dma-buf export sync file of the busy buffer: "
			    "%s\n", ename(e));
	}
	long_chain(&l);
	(void)submit(fa, l.va, &l.handle, 1, 0, 0);
	{
		struct dma_buf_sync ds;
		const char *w;

		ds.flags = DMA_BUF_SYNC_READ;
		e = xioctl(dl, DMA_BUF_IOCTL_SYNC, &ds);
		w = wait_bo(fa, l.handle, 0);
		printf("dma-buf sync start, the buffer busy: %s; then the "
		    "buffer: %s\n", ename(e), w);
	}
	{
		struct dma_buf_export_sync_file ex;

		memset(&ex, 0, sizeof(ex));
		ex.fd = -1;
		printf("dma-buf export sync file, flags 0: %s\n", ename(xioctl(dl,
		    DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex)));
	}
	long_chain(&l);
	(void)submit(fa, l.va, &l.handle, 1, so, 0);
	(void)so_export(fa, so, SYNC_FILE_FLAG, &frun);
	{
		struct dma_buf_import_sync_file im;
		const char *w1, *w2;

		memset(&im, 0, sizeof(im));
		im.flags = DMA_BUF_SYNC_WRITE;
		im.fd = frun;
		e = xioctl(d4, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &im);
		w1 = wait_bo(fa, h5, 0);
		w2 = wait_bo(fa, h5, now_ns() + 5000000000LL);
		printf("dma-buf import sync file into the shared buffer: %s; A's "
		    "wait_bo on it, deadline past: %s; then: %s\n", ename(e), w1,
		    w2);
		im.fd = p[0];
		printf("dma-buf import of a pipe: %s\n", ename(xioctl(d4,
		    DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &im)));
	}
	close(frun);

	/* Упавшее задание: его sync file, и задание после него. */
	write_value_job(m5b.m, 0, 1, 0, PRIME_FAULT_VA, FAULT_VALUE);
	{
		uint64_t va5 = 0;

		(void)offset_of(fa, h5, &va5);
		e = submit(fa, va5, &h5, 1, so, 0);
	}
	e2 = so_export(fa, so, SYNC_FILE_FLAG, &ff);
	{
		const char *r5 = readiness(ff, 5000), *i5 = file_info(ff);

		printf("a job that faults: submit %s, sync file %s; within 5 s: "
		    "%s, %s\n", ename(e), ename(e2), r5, i5);
	}
	sin2 = sync_create(fb, 0);
	(void)so_import(fb, ff, SYNC_FILE_FLAG, &sin2);
	write_value_job(cb.m, 0, 1, 0, vali + 0x3fe000, 0xb2b2b2b2u);
	{
		struct drm_panfrost_submit s;
		const char *sw;

		memset(&s, 0, sizeof(s));
		s.jc = cb.va;
		s.bo_handles = (uint64_t)(uintptr_t)hs;
		s.bo_handle_count = 2;
		s.in_syncs = (uint64_t)(uintptr_t)&sin2;
		s.in_sync_count = 1;
		s.out_sync = sout;
		e = xioctl(fb, DRM_IOCTL_PANFROST_SUBMIT, &s);
		sw = sync_wait(fb, &sout, 1, 0, now_ns() + 2000000000LL, NULL);
		printf("B's job after the failed one: submit %s, wait %s, its "
		    "word 0x%08x\n", ename(e), sw, *(uint32_t *)(l.m +
		    0x3fe000));
	}
	close(ff);
	close(dl);
	close(d4);
	close(fsig);
	close(sf);
	close(fa);
	close(fb);
	return 0;
}

int
main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc >= 2 && strcmp(argv[1], "api") == 0)
		return api();
	if (argc >= 2 && strcmp(argv[1], "two") == 0)
		return two();
	if (argc >= 2 && strcmp(argv[1], "prime") == 0)
		return prime();
	if (argc >= 2 && strcmp(argv[1], "heap") == 0)
		return heap();
	if (argc >= 2 && strcmp(argv[1], "chain") == 0)
		return chain(argc >= 3 ? (unsigned)atoi(argv[2]) : 1);
	if (argc >= 2 && strcmp(argv[1], "cost") == 0)
		return cost(argc >= 3 ? (unsigned)atoi(argv[2]) : 50);
	fprintf(stderr, "usage: pftest api | two | heap | chain [n] | "
	    "cost [n] | prime\n");
	return 2;
}
