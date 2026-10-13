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
 *	pftest chain [n]  одна цепочка из 64, n раз
 *
 * Заголовки - libdrm (drm.h) и ядра Linux (panfrost_drm.h), из
 * /usr/include/libdrm, как у Mesa; номера запросов - кодировки MINIX.
 * Отображение буфера - mmap(2) узла; libc сама спрашивает драйвер
 * (MIOCMMAP), когда VM говорит ENODEV.  VFS до 11.6 говорила EINVAL, и
 * тогда pftest спрашивает драйвер сам и пишет об этом строку.
 *
 * Собирается кросс-компилятором против DESTDIR:
 *	aarch64-elf64-minix-gcc -O -static -I$DEST/usr/include/libdrm \
 *	    --sysroot=$DEST -o pftest pftest.c
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ioc_mmap.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "drm.h"
#include "panfrost_drm.h"

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

int
main(int argc, char *argv[])
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc >= 2 && strcmp(argv[1], "api") == 0)
		return api();
	if (argc >= 2 && strcmp(argv[1], "two") == 0)
		return two();
	if (argc >= 2 && strcmp(argv[1], "chain") == 0)
		return chain(argc >= 3 ? (unsigned)atoi(argv[2]) : 1);
	fprintf(stderr, "usage: pftest api | two | chain [n]\n");
	return 2;
}
