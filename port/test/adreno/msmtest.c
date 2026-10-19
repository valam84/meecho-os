/*
 * Стенд узла рендеринга службы adreno (port/clover/hw-gpu.md, шаг 7):
 * /dev/dri/renderD128 как msm DRM, каким его видит Mesa freedreno, — на
 * модели планшета.  Настоящие файлы службы (adreno_drm.c, adreno_gpu.c и
 * всё под ними — такты, SMMU, zap, CP) и libdrmdriver; вместо железа —
 * модели model.c, smmumodel.c, tzmodel.c и cpmodel.c, вместо системы —
 * osmodel.c.  Служба под стенд не правится.
 *
 *	msmtest [-v] CLOVER_DTB
 *
 * Клиент — сам стенд: зовёт точки входа устройства, как их звал бы
 * libchardriver по запросам VFS (номер запроса — кодировки машины,
 * аргумент по гранту ровно того размера, что в номере, массивы — в его
 * памяти), видит буферы теми же страницами, что служба, и пишет в них
 * команды PM4, как Mesa.  Модель CP исполняет кольцо и буферы клиентов
 * через модель SMMU — его таблицы, TLB и линию отказа — из ОЗУ, не из
 * кэша; модель памяти пишет назад только грязные строки.
 *
 * Что проверяется, кроме ответов на каждый запрос:
 *
 *   - форма задания в кольце — как a5xx_submit() Linux для одного кольца;
 *     сброс UCHE при смене клиента;
 *   - заборы: номер очереди, WAIT_FENCE по линии 332, sync file и sync
 *     objects; ожидания со сроком и без;
 *   - изоляция: у клиентов свои таблицы; один и тот же адрес — разные
 *     страницы; задание другого клиента — только когда GPU простаивает,
 *     и после смены таблицы TLB его не помнит;
 *   - кэш: перед заданием написанное клиентом — в ОЗУ, после — снова из ОЗУ;
 *   - отказ SMMU в задании (линия 361): задание отказывает, счётчики
 *     FAULTS, следующее задание живёт;
 *   - зависание: задание без движения — восстановление (GPU выключен и
 *     поднят, zap возобновлён), задания после идут;
 *   - память: буфер и таблица уходят только тогда, когда SMMU их больше
 *     не достанет (модель проверяет при каждом освобождении); в конце
 *     ничего не осталось, и всё — как было до службы.
 */

#include "adreno_drm.h"
#include "adreno_regs.h"
#include "model.h"
#include "osmodel.h"

#include <minix/qcomscm.h>
#include <minix/timers.h>

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "drm.h"
#include "msm_drm.h"
#include <sys/ioc_mmap.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>

struct log ad_log = { "adreno", LEVEL_INFO, NULL };
int ad_route = 1;

static int checks, failures;

static void
check(int ok, const char *what, ...)
{
	va_list ap;

	checks++;
	if (ok)
		return;
	failures++;
	printf("  FAIL: ");
	va_start(ap, what);
	vprintf(what, ap);
	va_end(ap);
	printf("\n");
}

#define USER_A		0x100a
#define USER_B		0x100b
#define USER_C		0x100c
#define RENDER		DRMDRV_RENDER_MINOR
#define CP_MEM_WRITE	0x3d
#define CP_NOP		0x10
#define FAR_AWAY	0x7f000000ull	/* never mapped */

/* ---- the files: microcode and a zap shader, made up ------------------- */

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void
put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
save(const char *dir, const char *name, const void *p, size_t n)
{
	char path[256];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((f = fopen(path, "wb")) == NULL || fwrite(p, 1, n, f) != n) {
		fprintf(stderr, "msmtest: cannot write %s\n", path);
		exit(2);
	}
	fclose(f);
}

static void
put_ph(uint8_t *p, uint32_t type, uint32_t off, uint32_t paddr,
	uint32_t filesz, uint32_t memsz, uint32_t flags)
{
	put32(p + 0, type);
	put32(p + 4, off);
	put32(p + 8, paddr);
	put32(p + 12, paddr);
	put32(p + 16, filesz);
	put32(p + 20, memsz);
	put32(p + 24, flags);
	put32(p + 28, type == 1 ? 0x1000 : 0);
}

/* As adrtest.c's: the tablet's shape, a split relocatable image. */
#define S_HDR		0x94
#define S_HASH		0x988
#define S_SEG		0x1331
#define S_MEM		0x1800
#define MC_LEN		0x400

static uint8_t syn_mdt[S_HDR + S_HASH], syn_seg[S_SEG], syn_image[S_MEM];
static uint8_t syn_pm4[MC_LEN], syn_pfp[MC_LEN];

static void
make_files(const char *dir)
{
	uint8_t *h = syn_mdt;
	unsigned i;

	memset(syn_mdt, 0, sizeof(syn_mdt));
	memcpy(h, "\177ELF", 4);
	h[4] = 1;
	h[5] = 1;
	h[6] = 1;
	put16(h + 16, 2);
	put16(h + 18, 164);
	put32(h + 20, 1);
	put32(h + 24, 0x5000);
	put32(h + 28, 52);
	put32(h + 36, 0x73);
	put16(h + 40, 52);
	put16(h + 42, 32);
	put16(h + 44, 3);
	put_ph(h + 52, 0, 0, 0, S_HDR, 0, 0x07000000);
	put_ph(h + 84, 0, 0x1000, 0x7000, S_HASH, 0x1000, 0x02200000);
	put_ph(h + 116, 1, 0x3000, 0x5000, S_SEG, S_MEM, 0x08000007);
	for (i = 0; i < S_HASH; i++)
		h[S_HDR + i] = (uint8_t)(i * 7 + 3);
	for (i = 0; i < S_SEG; i++)
		syn_seg[i] = (uint8_t)(i * 13 + 1);
	memset(syn_image, 0, sizeof(syn_image));
	memcpy(syn_image, syn_seg, S_SEG);
	save(dir, "a512_zap.mdt", syn_mdt, sizeof(syn_mdt));
	save(dir, "a512_zap.b00", syn_mdt, S_HDR);
	save(dir, "a512_zap.b01", syn_mdt + S_HDR, S_HASH);
	save(dir, "a512_zap.b02", syn_seg, S_SEG);
	tz_expect(syn_mdt, sizeof(syn_mdt), syn_image, sizeof(syn_image),
	    0x2000);

	for (i = 0; i < MC_LEN; i++) {
		syn_pm4[i] = (uint8_t)(i * 5 + 7);
		syn_pfp[i] = (uint8_t)(i * 11 + 3);
	}
	put32(syn_pfp + 4, 0x005ff08a);
	put32(syn_pfp + 12, 0x00087001);
	save(dir, "a530_pm4.fw", syn_pm4, sizeof(syn_pm4));
	save(dir, "a530_pfp.fw", syn_pfp, sizeof(syn_pfp));
	cp_expect(syn_pm4, sizeof(syn_pm4), syn_pfp, sizeof(syn_pfp));
}

/* ---- the service, as RS starts and stops it --------------------------- */

/*
 * The tablet's SMMU (hw-gpu.md, §13-§16): "measured" as it is - a
 * fault-type S2CR write ignored, so that the route cannot be taken back
 * whole - and "tablet", the same taking every write, where everything
 * must be as found after the service.
 */
static const void *dtb;
static const struct scenario tablet = { "tablet", 1, 0, 0, 0, 0, 0, 0, 1,
    .smmu_tablet = 1, .stale_tlb = 1 };
static const struct scenario measured = { "tablet-measured", 1, 0, 0, 0, 0,
    0, 0, 1, .smmu_tablet = 1, .stale_tlb = 1, .s2cr_ro = 1 };
/* step 10: another speed bin; a machine without qcom_rpm. */
static const struct scenario bin122 = { "tablet-bin122", 1, 0, 0, 0, 0, 0, 0,
    1, .smmu_tablet = 1, .stale_tlb = 1, .speed_fuse = 122 };
static const struct scenario norpm = { "tablet-norpm", 1, 0, 0, 0, 0, 0, 0,
    1, .smmu_tablet = 1, .stale_tlb = 1, .no_rpm = 1 };

static void
serve_up_on(const struct scenario *sc)
{
	int r;

	model_reset(sc);
	osm_reset();
	smmu_model_serving(1);
	osm_cdev_close = adreno_chardriver.cdr_close;
	r = adreno_find(dtb, &ad.dt);
	if (r == OK) {
		adreno_find_report(&ad.dt);
		if ((r = adreno_map(dtb)) == OK && (r = cc_report()) == OK &&
		    (r = gpu_serve()) == OK) {
			adreno_drm_limits(768u << 20, 512u << 20);
			adreno_drm_init();
		}
	}
	check(r == OK, "start: %d", r);
}

static void
serve_up(void)
{
	serve_up_on(&tablet);
}

/* SIGTERM: what adreno.c's signal handler does. */
static void
serve_down(void)
{
	gpu_stop();
	adreno_drm_shutdown();
	gpu_down();
}

/* ---- the client ---------------------------------------------------------- */

static cdev_id_t next_id = 1, last_id;

static int
io(int minor, unsigned long req, void *arg, endpoint_t user)
{
	cp_grant_id_t g = osm_grant(arg, IOCPARM_LEN(req));

	last_id = next_id++;
	osm_user = user;
	return adreno_chardriver.cdr_ioctl(minor, req, VFS_PROC_NR, g, 0, user,
	    last_id);
}

/* open(2) of the node: a context, and the client's descriptor of it. */
static int
open_ctx(endpoint_t user, int *fd)
{
	int r = adreno_chardriver.cdr_open(RENDER, 0, user);

	check(r >= 0 && (r & CDEV_CLONED), "open: %d", r);
	r &= ~CDEV_CLONED;
	*fd = osm_fd_device(user, r);
	return r;
}

/* The kernel's side: lines up and enabled go to the service. */
static void
deliver(void)
{
	uint64_t m;
	unsigned n = 0;

	while ((m = smmu_model_lines()) != 0) {
		gpu_intr(m);
		if (++n > 100)
			model_fail("a line that never falls: the service "
			    "does not clear its source");
	}
}

/* Time goes on: the service's timers, then its lines. */
static void
advance(unsigned ms)
{
	micro_delay(ms * 1000ul);
	expire_timers(osm_ticks());
	deliver();
}

/* The GPU goes through what it was given. */
static void
gpu_go(void)
{
	cp_model_run();
	deliver();
}

struct cbo {
	uint32_t handle;
	uint64_t iova;
	uint32_t *cpu;
	size_t size;
};

static int
bo_new(int m, endpoint_t u, size_t size, uint32_t flags, struct cbo *b)
{
	struct drm_msm_gem_new g;
	struct drm_msm_gem_info i;
	int r;

	memset(&g, 0, sizeof(g));
	g.size = size;
	g.flags = flags;
	if ((r = io(m, DRM_IOCTL_MSM_GEM_NEW, &g, u)) != OK)
		return r;
	b->handle = g.handle;
	b->size = size;
	b->cpu = NULL;
	memset(&i, 0, sizeof(i));
	i.handle = g.handle;
	i.info = MSM_INFO_GET_IOVA;
	r = io(m, DRM_IOCTL_MSM_GEM_INFO, &i, u);
	b->iova = i.value;
	return r;
}

static int
bo_map(int m, endpoint_t u, struct cbo *b)
{
	struct drm_msm_gem_info i;
	struct cdev_mmap cm;
	int r;

	memset(&i, 0, sizeof(i));
	i.handle = b->handle;
	i.info = MSM_INFO_GET_OFFSET;
	if ((r = io(m, DRM_IOCTL_MSM_GEM_INFO, &i, u)) != OK)
		return r;
	memset(&cm, 0, sizeof(cm));
	cm.cm_len = b->size;
	cm.cm_prot = PROT_READ | PROT_WRITE;
	cm.cm_flags = MAP_SHARED;
	cm.cm_offset = (off_t)i.value;
	if ((r = io(m, MIOCMMAP, &cm, u)) != OK)
		return r;
	b->cpu = cm.cm_ret;
	return OK;
}

static int
gem_close(int m, endpoint_t u, uint32_t handle)
{
	struct drm_gem_close c;

	memset(&c, 0, sizeof(c));
	c.handle = handle;
	return io(m, DRM_IOCTL_GEM_CLOSE, &c, u);
}

static int
param(int m, endpoint_t u, uint32_t p, uint64_t *v)
{
	struct drm_msm_param a;
	int r;

	memset(&a, 0, sizeof(a));
	a.pipe = MSM_PIPE_3D0;
	a.param = p;
	r = io(m, DRM_IOCTL_MSM_GET_PARAM, &a, u);
	*v = a.value;
	return r;
}

/* PM4 into a client's buffer, as Mesa writes it: through its mapping. */
static unsigned ibn;

static void
ib_word(struct cbo *b, uint32_t v)
{
	b->cpu[ibn++] = v;
}

static void
ib_mem_write(struct cbo *b, uint64_t va, uint32_t v0, unsigned n)
{
	unsigned i;

	ib_word(b, pm4_pkt7(CP_MEM_WRITE, 2 + n));
	ib_word(b, (uint32_t)va);
	ib_word(b, (uint32_t)(va >> 32));
	for (i = 0; i < n; i++)
		ib_word(b, v0 + i);
}

static void
ib_hang(struct cbo *b)
{
	ib_word(b, pm4_pkt7(CP_NOP, 1));
	ib_word(b, CP_MODEL_HANG);
}

struct sub {
	uint32_t flags, queue;
	struct drm_msm_gem_submit_bo bos[8];
	unsigned nbos;
	struct drm_msm_gem_submit_cmd cmds[8];
	unsigned ncmds;
	int32_t fence_fd;
	struct drm_msm_syncobj in[4], out[4];
	unsigned nin, nout;
	uint32_t fence;
};

static void
sub_init(struct sub *s)
{
	memset(s, 0, sizeof(*s));
	s->fence_fd = -1;
}

static void
sub_bo(struct sub *s, struct cbo *b, uint32_t flags)
{
	s->bos[s->nbos].flags = flags;
	s->bos[s->nbos].handle = b->handle;
	s->nbos++;
}

/* A command buffer: bo number idx of the list, words [0, ibn). */
static void
sub_cmd(struct sub *s, unsigned idx, uint32_t off, uint32_t words,
	uint32_t type)
{
	s->cmds[s->ncmds].type = type;
	s->cmds[s->ncmds].submit_idx = idx;
	s->cmds[s->ncmds].submit_offset = off;
	s->cmds[s->ncmds].size = words * 4;
	s->ncmds++;
}

static int
submit(int m, endpoint_t u, struct sub *s)
{
	struct drm_msm_gem_submit a;
	int r;

	memset(&a, 0, sizeof(a));
	a.flags = MSM_PIPE_3D0 | s->flags;
	a.queueid = s->queue;
	a.nr_bos = s->nbos;
	a.bos = (uint64_t)(uintptr_t)s->bos;
	a.nr_cmds = s->ncmds;
	a.cmds = (uint64_t)(uintptr_t)s->cmds;
	a.fence_fd = s->fence_fd;
	a.nr_in_syncobjs = s->nin;
	a.in_syncobjs = (uint64_t)(uintptr_t)s->in;
	a.nr_out_syncobjs = s->nout;
	a.out_syncobjs = (uint64_t)(uintptr_t)s->out;
	a.syncobj_stride = sizeof(s->in[0]);
	r = io(m, DRM_IOCTL_MSM_GEM_SUBMIT, &a, u);
	s->fence = a.fence;
	s->fence_fd = a.fence_fd;
	return r;
}

/* The simplest job: one buffer of commands, the buffers it writes. */
static int
job(int m, endpoint_t u, struct cbo *cmd, struct cbo *target,
	uint32_t *fence)
{
	struct sub s;
	int r;

	sub_init(&s);
	sub_bo(&s, cmd, MSM_SUBMIT_BO_READ);
	if (target != NULL)
		sub_bo(&s, target, MSM_SUBMIT_BO_WRITE);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	r = submit(m, u, &s);
	*fence = s.fence;
	return r;
}

static int
wait_fence(int m, endpoint_t u, uint32_t queue, uint32_t fence,
	int64_t deadline)
{
	struct drm_msm_wait_fence w;

	memset(&w, 0, sizeof(w));
	w.fence = fence;
	w.queueid = queue;
	w.timeout.tv_sec = deadline / 1000000000LL;
	w.timeout.tv_nsec = deadline % 1000000000LL;
	return io(m, DRM_IOCTL_MSM_WAIT_FENCE, &w, u);
}

/* A wait that went to sleep: its answer, once there is one (or -1). */
static int
reply_of(cdev_id_t id)
{
	int r;

	return osm_reply(id, &r) ? r : -1;
}

/* A sync file's status, as SYNC_IOC_FILE_INFO tells it. */
static int
sync_status(endpoint_t u, int fd)
{
	struct sync_file_info si;
	int minor = osm_fd_minor(u, fd), r;

	memset(&si, 0, sizeof(si));
	if ((r = io(minor, SYNC_IOC_FILE_INFO, &si, u)) != OK)
		return 1000 + r;
	return si.status;
}

static uint64_t
queue_faults(int m, endpoint_t u, uint32_t q)
{
	struct drm_msm_submitqueue_query a;
	uint64_t v = 0;
	int r;

	memset(&a, 0, sizeof(a));
	a.id = q;
	a.param = MSM_SUBMITQUEUE_PARAM_FAULTS;
	a.data = (uint64_t)(uintptr_t)&v;
	a.len = sizeof(v);
	r = io(m, DRM_IOCTL_MSM_SUBMITQUEUE_QUERY, &a, u);
	return r == OK ? v : 1000000 + (uint64_t)r;
}

static uint64_t
global_faults(int m, endpoint_t u)
{
	uint64_t v = 0;

	(void)param(m, u, MSM_PARAM_FAULTS, &v);
	return v;
}

/* ---- the scenarios ---------------------------------------------------- */

/* Up and down, and nothing else: as found after. */
static void
t_start(void)
{
	char why[160];

	printf("start\n");
	serve_up();
	check(model_saw("smmu: cb0 over tables"), "start: no smmu line");
	check(model_saw("Q22 the GPU ran its first command"), "start: no Q22");
	check(model_saw("/dev/dri/renderD128 is served (msm 1.6): A512, chip "
	    "0x05010200, gmem 0x44000 at 0x100000"), "start: no served line");
	check(strcmp(tz_trail(), "init mem auth") == 0, "start: tz \"%s\"",
	    tz_trail());
	serve_down();
	check(model_saw("Q23 cp: interrupts masked, vbif ports halted"),
	    "stop: no Q23");
	check(model_saw("Q11 undone:") && model_saw("all as found"),
	    "stop: not as found");
	check(model_same_as_start(why, sizeof(why)), "stop: %s", why);
	check(model_pages_live() == 0, "stop: %u pages left",
	    model_pages_live());

	/* As measured: the route's S2CR stays, bank 0 off and stage 1. */
	serve_up_on(&measured);
	check(model_saw("/dev/dri/renderD128 is served"), "measured: not "
	    "served");
	serve_down();
	check(model_saw("Q17 smmu put back: smr 0 (s2cr left 00000300) cb0 "
	    "off, kept stage 1; all as found"), "measured: no Q17");
	check(model_saw("Q11 undone:") && model_pages_live() == 0 &&
	    model_contig_live() == 0, "measured: not all down");
}

/*
 * The core's clock (step 10): freq= takes the GPU off the crystal onto a
 * PLL, as Linux does - the PLL the root is not on, locked before the root
 * moves, the one left switched off - and everything goes back as found.
 * A rate whose VDDMX level is above the boot's, or one the tree does not
 * name, is refused and the crystal kept.  The model stops the run on a
 * PLL written while it runs, a root moved to a source that does not run,
 * a PLL stopped under the core, a rate not of Linux's table, or one above
 * what VDDMX holds.
 */
static void
t_clock(void)
{
	struct cbo cmd, tgt;
	uint32_t f = 0;
	char why[160];
	int a, fa;

	printf("the core's clock\n");
	ad_freq_mhz = 160;
	serve_up();
	check(model_saw("Q24 gfx3d 160 MHz: pll0 at 640 MHz (L 21, alpha "
	    "5555555600, band 2) locked in 20 us, the root on it /4; was 19 "
	    "MHz"), "160: no Q24 line");
	check((model_core_khz() + 500) / 1000 == 160, "160: the model has %u kHz",
	    model_core_khz());
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &tgt) == OK &&
	    bo_map(a, USER_A, &cmd) == OK && bo_map(a, USER_A, &tgt) == OK,
	    "160: buffers");
	ibn = 0;
	ib_mem_write(&cmd, tgt.iova + 16, 0x16000000, 2);
	check(job(a, USER_A, &cmd, &tgt, &f) == OK, "160: a job");
	deliver();
	check(tgt.cpu[4] == 0x16000000 && tgt.cpu[5] == 0x16000001,
	    "160: the job's writes %x %x", tgt.cpu[4], tgt.cpu[5]);

	/* Once more: the other PLL, the first one off. */
	model_log_reset();
	check(cc_gfx3d_set(160) == OK && model_saw("Q24 gfx3d 160 MHz: pll1 "
	    "at 640 MHz") && model_saw("was 160 MHz, the PLL it left off"),
	    "160 again: not on pll1");
	model_log_reset();
	check(cc_gfx3d_set(19) == OK && model_core_khz() == 19200 &&
	    model_saw("Q24 gfx3d 19.2 MHz: the crystal, the PLL it left off"),
	    "the crystal: %u kHz", model_core_khz());
	model_log_reset();
	check(cc_gfx3d_set(160) == OK && model_saw("Q24 gfx3d 160 MHz: pll0 "
	    "at 640 MHz"), "from the crystal: not pll0");
	ibn = 0;
	ib_mem_write(&cmd, tgt.iova + 16, 0x16100000, 1);
	check(job(a, USER_A, &cmd, &tgt, &f) == OK, "after the moves: a job");
	deliver();
	check(tgt.cpu[4] == 0x16100000, "after the moves: %x", tgt.cpu[4]);
	osm_proc_exit(USER_A);
	serve_down();
	check(model_saw("Q11 undone:") && model_saw("all as found"),
	    "160: not as found");
	check(model_same_as_start(why, sizeof(why)), "160: %s", why);

	check(!model_rpm_on(), "160: VDDMX still voted");

	/* 266: VDDMX up to LOW_SVS first, then the clock. */
	ad_freq_mhz = 266;
	serve_up();
	check(model_saw("Q25 vddmx: level 64 voted (was 0)") &&
	    model_saw("Q24 gfx3d 266 MHz: pll0 at 532 MHz"), "266: not up");
	check((model_core_khz() + 500) / 1000 == 266 &&
	    model_rpm_level() == 64, "266: %u kHz, level %u", model_core_khz(),
	    model_rpm_level());
	serve_down();
	check(model_same_as_start(why, sizeof(why)), "266: %s", why);

	/* max: the highest point the bin allows - fuse 0, as 157: 588. */
	ad_freq_mhz = 1;
	serve_up();
	check(model_saw("Q26 speed bin: fuse 0, opp-supported-hw 0x40: points "
	    "up to 588 MHz"), "max: no Q26");
	check(model_saw("Q25 vddmx: level 256 voted") && model_saw("Q24 gfx3d "
	    "588 MHz: pll0 at 1176 MHz (L 3d, alpha 4000000000, band 0)"),
	    "max: not 588");
	check((model_core_khz() + 500) / 1000 == 588, "max: %u kHz",
	    model_core_khz());
	serve_down();
	check(model_same_as_start(why, sizeof(why)), "max: %s", why);

	/* Not a point of the tree. */
	ad_freq_mhz = 500;
	serve_up();
	check(model_saw("gpu: freq=500 is not an operating point the tree "
	    "gives this bin"), "500: not refused");
	check(model_core_khz() == 19200, "500: the model has %u kHz",
	    model_core_khz());
	serve_down();

	/* Bin 122 (up to 585 in Linux's words; 465 of this tree's points). */
	ad_freq_mhz = 588;
	serve_up_on(&bin122);
	check(model_saw("Q26 speed bin: fuse 122, opp-supported-hw 0x8: points "
	    "up to 465 MHz") && model_saw("gpu: freq=588 is not an operating "
	    "point the tree gives this bin"), "bin 122: 588 not refused");
	check(model_core_khz() == 19200, "bin 122: %u kHz", model_core_khz());
	serve_down();
	ad_freq_mhz = 1;
	serve_up_on(&bin122);
	check((model_core_khz() + 500) / 1000 == 465 && model_rpm_level() ==
	    192, "bin 122, max: %u kHz, level %u", model_core_khz(),
	    model_rpm_level());
	serve_down();

	/* No qcom_rpm: no vote goes; only what the boot's MIN_SVS holds. */
	ad_freq_mhz = 266;
	serve_up_on(&norpm);
	check(model_saw("gpu: 266 MHz wants VDDMX at level 64 and the vote "
	    "did not go"), "no rpm: 266 not refused");
	check(model_core_khz() == 19200, "no rpm: %u kHz", model_core_khz());
	serve_down();
	ad_freq_mhz = 160;
	serve_up_on(&norpm);
	check((model_core_khz() + 500) / 1000 == 160, "no rpm, 160: %u kHz",
	    model_core_khz());
	serve_down();
	check(model_same_as_start(why, sizeof(why)), "no rpm: %s", why);
	ad_freq_mhz = 0;
}

static void
t_params(void)
{
	struct drm_version v;
	struct drm_get_cap cap;
	struct drm_msm_param p;
	char name[16], date[16], desc[32];
	uint64_t x;
	int a, fa;

	printf("params\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	memset(&v, 0, sizeof(v));
	v.name = name;
	v.name_len = sizeof(name);
	v.date = date;
	v.date_len = sizeof(date);
	v.desc = desc;
	v.desc_len = sizeof(desc);
	memset(name, 0, sizeof(name));
	memset(date, 0, sizeof(date));
	memset(desc, 0, sizeof(desc));
	check(io(a, DRM_IOCTL_VERSION, &v, USER_A) == OK &&
	    v.version_major == 1 && v.version_minor == 6 &&
	    strcmp(name, "msm") == 0 && strcmp(desc, "MSM Snapdragon DRM") ==
	    0, "version: %d.%d %s %s", v.version_major, v.version_minor, name,
	    desc);
	memset(&cap, 0, sizeof(cap));
	cap.capability = DRM_CAP_SYNCOBJ;
	check(io(a, DRM_IOCTL_GET_CAP, &cap, USER_A) == OK && cap.value == 1,
	    "cap syncobj");
	check(param(a, USER_A, MSM_PARAM_GPU_ID, &x) == OK && x == 512,
	    "gpu id %llu", (unsigned long long)x);
	check(param(a, USER_A, MSM_PARAM_CHIP_ID, &x) == OK &&
	    x == 0x05010200, "chip id %llx", (unsigned long long)x);
	check(param(a, USER_A, MSM_PARAM_GMEM_SIZE, &x) == OK && x == 0x44000,
	    "gmem size");
	check(param(a, USER_A, MSM_PARAM_GMEM_BASE, &x) == OK &&
	    x == 0x100000, "gmem base");
	check(param(a, USER_A, MSM_PARAM_PRIORITIES, &x) == OK && x == 3,
	    "priorities");
	check(param(a, USER_A, MSM_PARAM_MAX_FREQ, &x) == OK &&
	    x == 19200000, "max freq");
	check(param(a, USER_A, MSM_PARAM_TIMESTAMP, &x) == OK &&
	    x == model_now_us() * 192 / 10, "timestamp %llu",
	    (unsigned long long)x);
	check(param(a, USER_A, MSM_PARAM_FAULTS, &x) == OK && x == 0,
	    "faults");
	check(param(a, USER_A, MSM_PARAM_UCHE_TRAP_BASE, &x) == OK &&
	    x == 0x0001ffffffff0000ull, "trap base");
	check(param(a, USER_A, MSM_PARAM_HIGHEST_BANK_BIT, &x) == OK &&
	    x == 14, "bank bit");
	check(param(a, USER_A, MSM_PARAM_VA_START, &x) == OK &&
	    x == AD_VA_START, "va start");
	check(param(a, USER_A, 0x99, &x) == EINVAL, "unknown param");
	memset(&p, 0, sizeof(p));
	p.pipe = MSM_PIPE_2D0;
	p.param = MSM_PARAM_GPU_ID;
	check(io(a, DRM_IOCTL_MSM_GET_PARAM, &p, USER_A) == EINVAL,
	    "pipe 2d");
	p.pipe = MSM_PIPE_3D0;
	p.len = 4;
	check(io(a, DRM_IOCTL_MSM_GET_PARAM, &p, USER_A) == EINVAL,
	    "a length for a value");
	osm_proc_exit(USER_A);
	serve_down();
}

static void
t_buffers(void)
{
	struct drm_msm_gem_info i;
	struct drm_msm_gem_madvise mv;
	struct drm_msm_gem_cpu_prep cp;
	struct cbo b1, b2, b3;
	char nm[32];
	int a, fa;

	printf("buffers\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 5000, MSM_BO_WC, &b1) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &b2) == OK, "gem_new");
	check(b1.iova >= AD_VA_START && (b1.iova & 4095) == 0 &&
	    b2.iova >= b1.iova + 8192, "iovas %llx %llx",
	    (unsigned long long)b1.iova, (unsigned long long)b2.iova);
	check(bo_new(a, USER_A, 4096, MSM_BO_CACHED_COHERENT, &b3) == EINVAL,
	    "cached coherent");
	check(bo_new(a, USER_A, 0, MSM_BO_WC, &b3) == EINVAL, "size 0");
	check(bo_new(a, USER_A, 4096, 0x100, &b3) == EINVAL, "bad flags");
	memset(&i, 0, sizeof(i));
	i.handle = 99;
	i.info = MSM_INFO_GET_IOVA;
	check(io(a, DRM_IOCTL_MSM_GEM_INFO, &i, USER_A) == ENOENT,
	    "unknown handle");
	i.handle = b1.handle;
	i.info = MSM_INFO_GET_IOVA;
	i.len = 8;
	check(io(a, DRM_IOCTL_MSM_GEM_INFO, &i, USER_A) == EINVAL,
	    "a length for a value");
	i.len = 0;
	i.info = MSM_INFO_GET_FLAGS;
	check(io(a, DRM_IOCTL_MSM_GEM_INFO, &i, USER_A) == OK &&
	    i.value == MSM_BO_WC, "flags");
	strcpy(nm, "vertices");
	i.info = MSM_INFO_SET_NAME;
	i.value = (uint64_t)(uintptr_t)nm;
	i.len = 8;
	check(io(a, DRM_IOCTL_MSM_GEM_INFO, &i, USER_A) == OK, "set name");
	memset(nm, 0, sizeof(nm));
	i.info = MSM_INFO_GET_NAME;
	i.len = sizeof(nm);
	check(io(a, DRM_IOCTL_MSM_GEM_INFO, &i, USER_A) == OK && i.len == 8 &&
	    strcmp(nm, "vertices") == 0, "get name \"%s\"", nm);
	i.info = MSM_INFO_SET_NAME;
	i.len = 32;
	check(io(a, DRM_IOCTL_MSM_GEM_INFO, &i, USER_A) == EINVAL,
	    "a name with no room for its zero");
	i.info = 77;
	i.len = 0;
	check(io(a, DRM_IOCTL_MSM_GEM_INFO, &i, USER_A) == EINVAL,
	    "unknown info");

	/* The mapping: the client writes, sees what it wrote. */
	check(bo_map(a, USER_A, &b1) == OK && b1.cpu != NULL, "map");
	check(b1.cpu != NULL && b1.cpu[0] == 0 && b1.cpu[1023] == 0,
	    "a new buffer is zeros");
	if (b1.cpu != NULL)
		b1.cpu[5] = 0x12345678;
	memset(&mv, 0, sizeof(mv));
	mv.handle = b1.handle;
	mv.madv = MSM_MADV_DONTNEED;
	check(io(a, DRM_IOCTL_MSM_GEM_MADVISE, &mv, USER_A) == OK &&
	    mv.retained == 1, "madvise");
	mv.madv = 7;
	check(io(a, DRM_IOCTL_MSM_GEM_MADVISE, &mv, USER_A) == EINVAL,
	    "madvise 7");
	memset(&cp, 0, sizeof(cp));
	cp.handle = b1.handle;
	cp.op = MSM_PREP_READ | MSM_PREP_NOSYNC;
	check(io(a, DRM_IOCTL_MSM_GEM_CPU_PREP, &cp, USER_A) == OK,
	    "cpu_prep idle");
	cp.op = 0x100;
	check(io(a, DRM_IOCTL_MSM_GEM_CPU_PREP, &cp, USER_A) == EINVAL,
	    "cpu_prep bad op");
	check(gem_close(a, USER_A, b2.handle) == OK, "gem_close");
	check(gem_close(a, USER_A, b2.handle) == EINVAL, "gem_close twice");
	osm_proc_exit(USER_A);
	serve_down();
	check(model_pages_live() == 0, "buffers: %u pages left",
	    model_pages_live());
}

/* One job, as Mesa makes one: commands in a buffer, a buffer they write. */
static void
t_job(void)
{
	struct cbo cmd, tgt;
	struct sub s;
	uint32_t f1, f2;
	int a, fa, r;

	printf("a job\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &tgt) == OK &&
	    bo_map(a, USER_A, &cmd) == OK && bo_map(a, USER_A, &tgt) == OK,
	    "buffers");
	ibn = 0;
	ib_mem_write(&cmd, tgt.iova + 16, 0xcafe0000, 4);
	cp_model_trail_reset();
	sub_init(&s);
	sub_bo(&s, &cmd, MSM_SUBMIT_BO_READ);
	sub_bo(&s, &tgt, MSM_SUBMIT_BO_WRITE);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_FENCE_FD_OUT;
	r = submit(a, USER_A, &s);
	f1 = s.fence;
	check(r == OK && f1 == 1 && s.fence_fd >= 3, "submit: %d, fence %u, "
	    "fd %d", r, f1, s.fence_fd);
	deliver();
	check(strcmp(cp_model_trail(), "prot0 save_addr prot1 uche_inval wfi "
	    "ib scratch2=2 fence2 where") == 0, "the ring: \"%s\"",
	    cp_model_trail());
	check(tgt.cpu[4] == 0xcafe0000 && tgt.cpu[7] == 0xcafe0003 &&
	    tgt.cpu[3] == 0 && tgt.cpu[8] == 0, "the GPU's writes: %x %x",
	    tgt.cpu[4], tgt.cpu[7]);
	check(wait_fence(a, USER_A, 0, f1, osm_mono_ns() + 1000000000LL) ==
	    OK, "wait_fence 1");
	check(sync_status(USER_A, s.fence_fd) == 1, "sync file: %d",
	    sync_status(USER_A, s.fence_fd));

	/* The same client again: no switch, no UCHE invalidation. */
	ibn = 0;
	ib_mem_write(&cmd, tgt.iova + 16, 0xbeef0000, 1);
	cp_model_trail_reset();
	f2 = 0;
	r = job(a, USER_A, &cmd, &tgt, &f2);
	check(r == OK && f2 == 2, "second: %d, fence %u", r, f2);
	deliver();
	check(strcmp(cp_model_trail(), "prot0 save_addr prot1 ib scratch2=3 "
	    "fence3 where") == 0, "the ring again: \"%s\"", cp_model_trail());
	check(tgt.cpu[4] == 0xbeef0000, "the second write: %x", tgt.cpu[4]);
	check(wait_fence(a, USER_A, 0, 3, osm_mono_ns()) == EINVAL,
	    "a fence not given");
	check(wait_fence(a, USER_A, 5, 1, osm_mono_ns()) == ENOENT,
	    "no queue 5");
	osm_proc_exit(USER_A);
	serve_down();
}

/* Waits: the GPU busy, a wait that sleeps, one that times out. */
static void
t_waits(void)
{
	struct drm_msm_gem_cpu_prep cp;
	struct cbo cmd, tgt;
	uint32_t f;
	cdev_id_t w1, w2;
	int a, fa, r;

	printf("waits\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &tgt) == OK &&
	    bo_map(a, USER_A, &cmd) == OK, "buffers");
	cp_model_defer(1);
	ibn = 0;
	ib_mem_write(&cmd, tgt.iova, 7, 1);
	check(job(a, USER_A, &cmd, &tgt, &f) == OK, "submit");
	r = wait_fence(a, USER_A, 0, f, osm_mono_ns() + 500000000LL);
	w1 = last_id;
	check(r == EDONTREPLY, "a wait on a job in the GPU sleeps: %d", r);
	r = wait_fence(a, USER_A, 0, f, osm_mono_ns() + 30000000LL);
	w2 = last_id;
	check(r == EDONTREPLY, "a short wait sleeps: %d", r);
	check(wait_fence(a, USER_A, 0, f, osm_mono_ns()) == ETIMEDOUT,
	    "a deadline past: ETIMEDOUT");
	memset(&cp, 0, sizeof(cp));
	cp.handle = tgt.handle;
	cp.op = MSM_PREP_WRITE | MSM_PREP_NOSYNC;
	cp.timeout.tv_sec = osm_mono_ns() / 1000000000LL + 10;
	check(io(a, DRM_IOCTL_MSM_GEM_CPU_PREP, &cp, USER_A) == EBUSY,
	    "cpu_prep nosync, a deadline ahead: EBUSY at once");
	advance(50);
	check(reply_of(w2) == ETIMEDOUT, "the short wait: %d", reply_of(w2));
	check(reply_of(w1) == -1, "the long wait answered early");
	gpu_go();
	check(reply_of(w1) == OK, "the long wait: %d", reply_of(w1));
	cp_model_defer(0);
	osm_proc_exit(USER_A);
	serve_down();
}

static void
t_queues(void)
{
	struct drm_msm_submitqueue q;
	uint32_t id;
	struct cbo cmd;
	struct sub s;
	int a, fa;

	printf("queues\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	memset(&q, 0, sizeof(q));
	q.prio = 3;
	check(io(a, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &q, USER_A) == EINVAL,
	    "prio 3");
	q.prio = 0;
	q.flags = MSM_SUBMITQUEUE_ALLOW_PREEMPT;
	check(io(a, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &q, USER_A) == EINVAL,
	    "preempt");
	q.flags = 0;
	check(io(a, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &q, USER_A) == OK &&
	    q.id == 1, "new: id %u", q.id);
	check(queue_faults(a, USER_A, 1) == 0, "faults of 1");
	check(queue_faults(a, USER_A, 9) == 1000000 + ENOENT, "faults of 9");
	id = 0;
	check(io(a, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &id, USER_A) == ENOENT,
	    "close 0");
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_map(a, USER_A, &cmd) == OK, "buffer");
	ibn = 0;
	ib_word(&cmd, pm4_pkt7(CP_NOP, 0));
	sub_init(&s);
	s.queue = 1;
	sub_bo(&s, &cmd, MSM_SUBMIT_BO_READ);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	check(submit(a, USER_A, &s) == OK && s.fence == 1, "submit to 1");
	deliver();
	id = 1;
	check(io(a, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &id, USER_A) == OK,
	    "close 1");
	check(submit(a, USER_A, &s) == ENOENT, "submit to a closed queue");
	osm_proc_exit(USER_A);
	serve_down();
}

/* What a submission must be, or EINVAL. */
static void
t_submit_errors(void)
{
	struct cbo cmd, other;
	struct sub s;
	int a, fa, b, fb;

	printf("submit errors\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	b = open_ctx(USER_B, &fb);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_map(a, USER_A, &cmd) == OK &&
	    bo_new(b, USER_B, 4096, MSM_BO_WC, &other) == OK, "buffers");
	ibn = 0;
	ib_word(&cmd, pm4_pkt7(CP_NOP, 0));
#define BAD(what, set, want) do {					\
	sub_init(&s);							\
	sub_bo(&s, &cmd, MSM_SUBMIT_BO_READ);				\
	sub_cmd(&s, 0, 0, 1, MSM_SUBMIT_CMD_BUF);			\
	set;								\
	check(submit(a, USER_A, &s) == (want), "%s: %d", what,		\
	    submit(a, USER_A, &s));					\
} while (0)
	BAD("sudo", s.flags = MSM_SUBMIT_SUDO, EINVAL);
	BAD("fence sn in", s.flags = MSM_SUBMIT_FENCE_SN_IN, EINVAL);
	BAD("no commands", s.ncmds = 0, EINVAL);
	BAD("a command type", s.cmds[0].type = 9, EINVAL);
	BAD("a size not of words", s.cmds[0].size = 6, EINVAL);
	BAD("past the buffer", s.cmds[0].submit_offset = 4096, EINVAL);
	BAD("an index past the list", s.cmds[0].submit_idx = 1, EINVAL);
	BAD("relocations", s.cmds[0].nr_relocs = 1, EINVAL);
	BAD("a buffer twice", sub_bo(&s, &cmd, MSM_SUBMIT_BO_READ), EINVAL);
	BAD("another client's handle", s.bos[0].handle = 2 + other.handle,
	    EINVAL);
	BAD("a bad buffer flag", s.bos[0].flags = 0x100, EINVAL);
	BAD("a fence fd that is none", (s.flags = MSM_SUBMIT_FENCE_FD_IN,
	    s.fence_fd = 77), EINVAL);
	BAD("a sync object that is none", (s.flags = MSM_SUBMIT_SYNCOBJ_IN,
	    s.nin = 1, s.in[0].handle = 5), EINVAL);
	BAD("a timeline point", (s.flags = MSM_SUBMIT_SYNCOBJ_IN, s.nin = 1,
	    s.in[0].point = 3), EOPNOTSUPP);
#undef BAD
	osm_proc_exit(USER_A);
	osm_proc_exit(USER_B);
	serve_down();
}

/* A fault in a job: line 361, the job fails, the next one lives. */
static void
t_fault(void)
{
	struct cbo cmd, tgt, ro;
	struct sub s;
	uint32_t f;
	int a, fa;

	printf("fault\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &tgt) == OK &&
	    bo_map(a, USER_A, &cmd) == OK && bo_map(a, USER_A, &tgt) == OK,
	    "buffers");
	ibn = 0;
	ib_mem_write(&cmd, FAR_AWAY, 0xdead, 1);
	ib_mem_write(&cmd, tgt.iova, 0x600d, 1);
	sub_init(&s);
	sub_bo(&s, &cmd, MSM_SUBMIT_BO_READ);
	sub_bo(&s, &tgt, MSM_SUBMIT_BO_WRITE);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_FENCE_FD_OUT;
	check(submit(a, USER_A, &s) == OK, "submit");
	/* The fence's line taken first, the fault's after: the job must
	 * still fail - the service looks at bank 0 before it retires. */
	gpu_intr(smmu_model_lines_of(1ull << AD_GPU_HOOK));
	deliver();
	check(model_saw("client 0: a fault at 7f000000 (fsr 00000002 fsynr0 "
	    "00000010)"), "no fault line in the log");
	check(model_saw("job 2 fails"), "the job not failed");
	check(tgt.cpu[0] == 0x600d, "the GPU went on after the fault: %x",
	    tgt.cpu[0]);
	check(wait_fence(a, USER_A, 0, s.fence, osm_mono_ns()) == OK,
	    "wait_fence: Linux's 0");
	check(sync_status(USER_A, s.fence_fd) == -EIO, "sync file: %d",
	    sync_status(USER_A, s.fence_fd));
	check(global_faults(a, USER_A) == 1 && queue_faults(a, USER_A, 0) ==
	    1, "faults %llu %llu", (unsigned long long)global_faults(a,
	    USER_A), (unsigned long long)queue_faults(a, USER_A, 0));

	ibn = 0;
	ib_mem_write(&cmd, tgt.iova + 4, 0x2222, 1);
	check(job(a, USER_A, &cmd, &tgt, &f) == OK, "the next job");
	deliver();
	check(tgt.cpu[1] == 0x2222 && queue_faults(a, USER_A, 0) == 1,
	    "the next job ran clean: %x", tgt.cpu[1]);

	/* A buffer the GPU may only read: a write to it is a permission
	 * fault, and the buffer stays as it was. */
	check(bo_new(a, USER_A, 4096, MSM_BO_WC | MSM_BO_GPU_READONLY, &ro) ==
	    OK && bo_map(a, USER_A, &ro) == OK, "a read-only buffer");
	ibn = 0;
	ib_mem_write(&cmd, ro.iova, 0x3333, 1);
	check(job(a, USER_A, &cmd, &ro, &f) == OK, "a write to it");
	deliver();
	check(ro.cpu[0] == 0 && queue_faults(a, USER_A, 0) == 2 &&
	    model_saw("(fsr 00000008"), "the read-only buffer written: %x",
	    ro.cpu[0]);
	osm_proc_exit(USER_A);
	serve_down();
}

/*
 * Two clients: their own spaces - the same address, their own pages - and
 * one client's jobs in the ring only after the other's are over and its
 * table is loaded, the UCHE invalidated, the TLB forgotten.
 */
static void
t_two(void)
{
	struct cbo ca, ta, cb, tb, a2;
	uint32_t fa1, fb1;
	unsigned ibs;
	int a, fda, b, fdb;

	printf("two clients\n");
	serve_up();
	a = open_ctx(USER_A, &fda);
	b = open_ctx(USER_B, &fdb);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &ta) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &ca) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &a2) == OK &&
	    bo_new(b, USER_B, 4096, MSM_BO_WC, &tb) == OK &&
	    bo_new(b, USER_B, 4096, MSM_BO_WC, &cb) == OK, "buffers");
	check(ta.iova == tb.iova && ca.iova == cb.iova, "the same addresses "
	    "in both spaces: %llx %llx", (unsigned long long)ta.iova,
	    (unsigned long long)tb.iova);
	check(bo_map(a, USER_A, &ca) == OK && bo_map(a, USER_A, &ta) == OK &&
	    bo_map(a, USER_A, &a2) == OK && bo_map(b, USER_B, &cb) == OK &&
	    bo_map(b, USER_B, &tb) == OK, "maps");

	cp_model_defer(1);
	ibn = 0;
	ib_mem_write(&ca, ta.iova, 0xaaaa0000, 2);
	check(job(a, USER_A, &ca, &ta, &fa1) == OK, "a's job");
	ibn = 0;
	ib_mem_write(&cb, tb.iova, 0xbbbb0000, 2);
	/* And b reaches for a2's address: not b's - a fault in b's space. */
	ib_mem_write(&cb, a2.iova, 0xbad, 1);
	check(job(b, USER_B, &cb, &tb, &fb1) == OK, "b's job");
	ibs = cp_model_ibs();
	cp_model_trail_reset();
	gpu_go();
	check(cp_model_ibs() == ibs + 1, "only a's job ran first: %u",
	    cp_model_ibs() - ibs);
	gpu_go();
	gpu_go();
	check(cp_model_ibs() == ibs + 2, "then b's: %u", cp_model_ibs() - ibs);
	check(cp_model_invals() >= 2 && strstr(cp_model_trail(),
	    "fence2 where prot0 save_addr prot1 uche_inval wfi ib") != NULL,
	    "b's job began with the UCHE: \"%s\"", cp_model_trail());
	check(ta.cpu[0] == 0xaaaa0000 && tb.cpu[0] == 0xbbbb0000 &&
	    ta.cpu[1] == 0xaaaa0001 && tb.cpu[1] == 0xbbbb0001,
	    "each its own: %x %x", ta.cpu[0], tb.cpu[0]);
	check(a2.cpu[0] == 0, "b wrote into a's buffer: %x", a2.cpu[0]);
	check(model_saw("client 1: a fault at"), "b's fault not seen");
	cp_model_defer(0);
	osm_proc_exit(USER_A);
	osm_proc_exit(USER_B);
	serve_down();
	check(model_pages_live() == 0, "two: %u pages left",
	    model_pages_live());
}

/* A buffer shared (PRIME): b's job after a's on it, by the implicit fence. */
static void
t_prime(void)
{
	struct drm_prime_handle ph;
	struct cbo ca, sh, cb, shb;
	uint32_t f;
	unsigned ibs;
	int a, fda, b, fdb, fd;

	printf("prime\n");
	serve_up();
	a = open_ctx(USER_A, &fda);
	b = open_ctx(USER_B, &fdb);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &ca) == OK &&
	    bo_new(a, USER_A, 8192, MSM_BO_WC, &sh) == OK &&
	    bo_map(a, USER_A, &ca) == OK && bo_map(a, USER_A, &sh) == OK &&
	    bo_new(b, USER_B, 4096, MSM_BO_WC, &cb) == OK &&
	    bo_map(b, USER_B, &cb) == OK, "buffers");
	memset(&ph, 0, sizeof(ph));
	ph.handle = sh.handle;
	ph.flags = DRM_CLOEXEC | DRM_RDWR;
	check(io(a, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph, USER_A) == OK,
	    "export");
	fd = osm_fd_pass(USER_A, ph.fd, USER_B);
	memset(&ph, 0, sizeof(ph));
	ph.fd = fd;
	check(io(b, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph, USER_B) == OK, "import");
	shb.handle = ph.handle;
	shb.size = 8192;
	{
		struct drm_msm_gem_info i;

		memset(&i, 0, sizeof(i));
		i.handle = shb.handle;
		i.info = MSM_INFO_GET_IOVA;
		check(io(b, DRM_IOCTL_MSM_GEM_INFO, &i, USER_B) == OK,
		    "iova in b");
		shb.iova = i.value;
	}

	cp_model_defer(1);
	ibn = 0;
	ib_mem_write(&ca, sh.iova + 64, 0x1111, 1);
	check(job(a, USER_A, &ca, &sh, &f) == OK, "a writes the buffer");
	ibn = 0;
	ib_mem_write(&cb, shb.iova + 128, 0x2222, 1);
	check(job(b, USER_B, &cb, &shb, &f) == OK, "b writes it");
	ibs = cp_model_ibs();
	gpu_go();
	gpu_go();
	gpu_go();
	check(cp_model_ibs() == ibs + 2, "both ran: %u", cp_model_ibs() - ibs);
	check(sh.cpu[16] == 0x1111 && sh.cpu[32] == 0x2222, "a sees both: %x "
	    "%x", sh.cpu[16], sh.cpu[32]);
	cp_model_defer(0);
	osm_proc_exit(USER_B);
	osm_proc_exit(USER_A);
	serve_down();
}

/* Fences across clients: a sync file in, sync objects in and out. */
static void
t_fences(void)
{
	struct drm_syncobj_create sc;
	struct drm_syncobj_wait sw;
	struct cbo ca, cb;
	struct sub s;
	uint32_t h;
	unsigned ibs;
	int a, fda, b, fdb, fd;

	printf("fences\n");
	serve_up();
	a = open_ctx(USER_A, &fda);
	b = open_ctx(USER_B, &fdb);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &ca) == OK &&
	    bo_map(a, USER_A, &ca) == OK &&
	    bo_new(b, USER_B, 4096, MSM_BO_WC, &cb) == OK &&
	    bo_map(b, USER_B, &cb) == OK, "buffers");
	memset(&sc, 0, sizeof(sc));
	check(io(a, DRM_IOCTL_SYNCOBJ_CREATE, &sc, USER_A) == OK, "syncobj");
	h = sc.handle;

	cp_model_defer(1);
	ibn = 0;
	ib_word(&ca, pm4_pkt7(CP_NOP, 0));
	sub_init(&s);
	sub_bo(&s, &ca, MSM_SUBMIT_BO_READ);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_FENCE_FD_OUT | MSM_SUBMIT_SYNCOBJ_OUT;
	s.nout = 1;
	s.out[0].handle = h;
	check(submit(a, USER_A, &s) == OK, "a's job, its fence out");
	fd = osm_fd_pass(USER_A, s.fence_fd, USER_B);

	ibn = 0;
	ib_word(&cb, pm4_pkt7(CP_NOP, 0));
	sub_init(&s);
	sub_bo(&s, &cb, MSM_SUBMIT_BO_READ);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_FENCE_FD_IN;
	s.fence_fd = fd;
	check(submit(b, USER_B, &s) == OK, "b's job, a's fence in");
	ibs = cp_model_ibs();
	gpu_go();
	check(cp_model_ibs() == ibs + 1, "a's alone first");
	gpu_go();
	gpu_go();
	check(cp_model_ibs() == ibs + 2, "then b's");

	/* The sync object a's job left: over; and as a job's in-fence,
	 * reset after. */
	memset(&sw, 0, sizeof(sw));
	sw.handles = (uint64_t)(uintptr_t)&h;
	sw.count_handles = 1;
	sw.timeout_nsec = osm_mono_ns();
	check(io(a, DRM_IOCTL_SYNCOBJ_WAIT, &sw, USER_A) == OK,
	    "the sync object over");
	sub_init(&s);
	sub_bo(&s, &ca, MSM_SUBMIT_BO_READ);
	sub_cmd(&s, 0, 0, 1, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_SYNCOBJ_IN;
	s.nin = 1;
	s.in[0].handle = h;
	s.in[0].flags = MSM_SYNCOBJ_RESET;
	check(submit(a, USER_A, &s) == OK, "with it in, reset");
	gpu_go();
	check(io(a, DRM_IOCTL_SYNCOBJ_WAIT, &sw, USER_A) == EINVAL,
	    "the sync object reset: no fence");
	cp_model_defer(0);
	osm_proc_exit(USER_A);
	osm_proc_exit(USER_B);
	serve_down();
}

/*
 * The governor (gov=, sleep=): the clock comes down when nothing runs, goes
 * up under work, VDDMX stays voted for the top all along; with sleep= the
 * GPU is powered off when idle and comes back for the next job - and in
 * between not a register of it is touched (the model's bus hangs on that).
 */
static void
gov_busy_job(int a, struct cbo *cmd, struct cbo *tgt, uint32_t v,
	unsigned busy_ms)
{
	uint32_t f;

	ibn = 0;
	ib_mem_write(cmd, tgt->iova, v, 1);
	check(job(a, USER_A, cmd, tgt, &f) == OK, "gov: a job");
	/* The line comes when the job is over: busy until then. */
	micro_delay(busy_ms * 1000ul);
	expire_timers(osm_ticks());
	deliver();
	check(tgt->cpu[0] == v, "gov: the job ran: %x, not %x", tgt->cpu[0], v);
}

static unsigned
core_mhz_now(void)
{
	return (model_core_khz() + 500) / 1000;
}

static void
gov_buffers(int *a, int *fa, struct cbo *cmd, struct cbo *tgt)
{
	*a = open_ctx(USER_A, fa);
	check(bo_new(*a, USER_A, 4096, MSM_BO_WC, cmd) == OK &&
	    bo_new(*a, USER_A, 4096, MSM_BO_WC, tgt) == OK &&
	    bo_map(*a, USER_A, cmd) == OK && bo_map(*a, USER_A, tgt) == OK,
	    "gov: buffers");
}

static void
t_gov(void)
{
	struct cbo cmd, tgt, b2;
	unsigned was = ad_freq_mhz, i;
	uint64_t x;
	char why[160];
	int a, fa;

	printf("the governor\n");
	ad_freq_mhz = 1;
	ad_gov = AD_GOV_ONDEMAND;
	ad_gov_ms = 50;
	ad_gov_downs = 2;
	ad_sleep_ms = 0;
	serve_up();
	check(model_saw("Q27 governor: ondemand, points 160 266 370 465 588 MHz"),
	    "no Q27 line with the bin's points");
	check(core_mhz_now() == 588 && model_rpm_level() == 256,
	    "start: %u MHz, level %u", core_mhz_now(), model_rpm_level());
	gov_buffers(&a, &fa, &cmd, &tgt);

	/* Idle: down after two periods, to the lowest point; the vote stays. */
	advance(50);
	check(core_mhz_now() == 588, "idle one period: %u MHz", core_mhz_now());
	advance(50);
	check(core_mhz_now() == 160 && model_rpm_level() == 256,
	    "idle two periods: %u MHz, level %u", core_mhz_now(),
	    model_rpm_level());
	advance(50);
	check(!osm_timers_pending(), "at rest at the lowest point, a timer "
	    "still armed");

	/* Busy: up at once, as far as it needs. */
	gov_busy_job(a, &cmd, &tgt, 0x601, 60);
	check(core_mhz_now() == 588, "busy at 160: %u MHz", core_mhz_now());
	/* Then quiet: down again. */
	for (i = 0; i < 4; i++)
		advance(50);
	check(core_mhz_now() == 160 && model_rpm_level() == 256,
	    "idle again: %u MHz, level %u", core_mhz_now(), model_rpm_level());
	/* The report: the clock as the registers have it. */
	ad_report_s = 1;
	model_log_reset();
	osm_proc_exit(USER_A);
	serve_down();
	serve_up();
	gov_buffers(&a, &fa, &cmd, &tgt);
	for (i = 0; i < 4; i++)
		advance(500);
	check(model_saw("; now 160000 kHz by the registers"),
	    "no report with the registers' 160 MHz");
	ad_report_s = 0;
	osm_proc_exit(USER_A);
	serve_down();
	check(model_same_as_start(why, sizeof(why)), "gov: %s", why);

	/* Sleep: off after sleep= ms with nothing, on for the next job. */
	ad_sleep_ms = 200;
	serve_up();
	gov_buffers(&a, &fa, &cmd, &tgt);
	gov_busy_job(a, &cmd, &tgt, 0x700, 0);
	for (i = 0; i < 10 && model_gpu_powered(); i++)
		advance(50);
	check(!model_gpu_powered() && i >= 4 && i <= 6,
	    "asleep after %u periods (powered %d)", i, model_gpu_powered());
	check(model_rpm_level() == 256, "asleep: the vote moved: %u",
	    model_rpm_level());
	/* The hang check's last look comes and goes; then nothing wakes it. */
	advance(2500);
	check(!osm_timers_pending() && !model_gpu_powered(),
	    "asleep, a timer still armed (powered %d)", model_gpu_powered());
	/* A line while it sleeps - another's, shared, or late: not its. */
	gpu_intr((uint64_t)1 << AD_GPU_HOOK);
	check(!model_gpu_powered(), "a line woke it");
	/* A buffer comes while it sleeps: no register touched. */
	check(bo_new(a, USER_A, 8192, MSM_BO_WC, &b2) == OK &&
	    bo_map(a, USER_A, &b2) == OK, "asleep: a new buffer");
	gov_busy_job(a, &cmd, &tgt, 0x701, 0);
	check(model_gpu_powered(), "the job did not wake it");
	check(strstr(tz_trail(), "remote") != NULL, "woken: the zap shader not "
	    "resumed: \"%s\"", tz_trail());
	/* Asleep again; a timestamp wakes it. */
	for (i = 0; i < 10 && model_gpu_powered(); i++)
		advance(50);
	check(!model_gpu_powered(), "not asleep the second time");
	check(param(a, USER_A, MSM_PARAM_TIMESTAMP, &x) == OK &&
	    model_gpu_powered(), "a timestamp while asleep");
	for (i = 0; i < 10 && model_gpu_powered(); i++)
		advance(50);
	check(!model_gpu_powered(), "not asleep the third time");
	/* The service goes while the GPU sleeps: everything as found. */
	osm_proc_exit(USER_A);
	check(smmu_loaded() == NULL, "asleep: bank 0 left on the table of a "
	    "client that went");
	serve_down();
	check(model_saw("3 sleeps;"), "no report of three sleeps");
	check(model_same_as_start(why, sizeof(why)), "sleep: %s", why);
	check(!model_rpm_on(), "sleep: VDDMX still voted");

	/* performance: the top, whatever the load; powersave: the lowest. */
	ad_sleep_ms = 0;
	ad_gov = AD_GOV_PERFORMANCE;
	serve_up();
	advance(50);
	advance(50);
	advance(50);
	check(core_mhz_now() == 588 && !osm_timers_pending(),
	    "performance: %u MHz", core_mhz_now());
	serve_down();
	ad_gov = AD_GOV_POWERSAVE;
	serve_up();
	check(core_mhz_now() == 160 && model_rpm_level() == 256,
	    "powersave: %u MHz, level %u", core_mhz_now(), model_rpm_level());
	serve_down();
	/* A cap: freq=370 - the points up to it, the vote for it. */
	ad_gov = AD_GOV_ONDEMAND;
	ad_freq_mhz = 370;
	serve_up();
	check(model_saw("points 160 266 370 MHz") && core_mhz_now() == 370 &&
	    model_rpm_level() == 128, "cap 370: %u MHz, level %u",
	    core_mhz_now(), model_rpm_level());
	serve_down();

	/* Changed while serving (sysctl minix.gpu): at once. */
	ad_freq_mhz = 1;
	ad_sleep_ms = 0;
	serve_up();
	check(gpu_policy(AD_GOV_POWERSAVE, 0) == OK && core_mhz_now() == 160,
	    "to powersave: %u MHz", core_mhz_now());
	check(gpu_policy(AD_GOV_PERFORMANCE, 0) == OK && core_mhz_now() == 588,
	    "to performance: %u MHz", core_mhz_now());
	check(gpu_policy(AD_GOV_ONDEMAND + 1, 0) == EINVAL &&
	    gpu_policy(AD_GOV_ONDEMAND, -1) == EINVAL, "a policy of nonsense");
	check(gpu_policy(AD_GOV_ONDEMAND, 200) == OK, "to sleep=200");
	for (i = 0; i < 20 && model_gpu_powered(); i++)
		advance(50);
	check(!model_gpu_powered(), "sleep=200 set while serving: not asleep");
	check(gpu_policy(AD_GOV_ONDEMAND, 0) == OK && model_gpu_powered(),
	    "sleep=0 does not wake it");
	{
		char st[256];

		gpu_status(st, sizeof(st));
		check(strstr(st, "kHz by the registers") != NULL, "status: %s",
		    st);
	}
	serve_down();
	check(model_same_as_start(why, sizeof(why)), "policy: %s", why);
	ad_gov = AD_GOV_ONDEMAND;
	ad_sleep_ms = 0;
	ad_freq_mhz = was;
}

/* A job that never ends: the hang check, the GPU brought up again. */
static void
t_hang(void)
{
	struct cbo cmd, tgt;
	struct sub s;
	uint32_t f;
	unsigned i;
	int a, fa;

	printf("hang\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &tgt) == OK &&
	    bo_map(a, USER_A, &cmd) == OK && bo_map(a, USER_A, &tgt) == OK,
	    "buffers");
	ibn = 0;
	ib_hang(&cmd);
	sub_init(&s);
	sub_bo(&s, &cmd, MSM_SUBMIT_BO_READ);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_FENCE_FD_OUT;
	check(submit(a, USER_A, &s) == OK, "the hanging job");
	deliver();
	check(cp_model_hung(), "the CP is stuck");
	advance(1000);
	advance(1000);
	check(!model_saw("a job made no progress"), "a hang called early");
	for (i = 0; i < 10 && !model_saw("a job made no progress"); i++)
		advance(1000);
	check(model_saw("a job made no progress") && i >= 2 && i <= 6,
	    "the hang called after %u more seconds", i);
	check(model_saw("up again after a hang (1 so far)"), "not up again");
	check(sync_status(USER_A, s.fence_fd) == -EIO, "its sync file: %d",
	    sync_status(USER_A, s.fence_fd));
	check(global_faults(a, USER_A) == 1 && queue_faults(a, USER_A, 0) ==
	    1, "faults %llu %llu", (unsigned long long)global_faults(a,
	    USER_A), (unsigned long long)queue_faults(a, USER_A, 0));
	check(strstr(tz_trail(), "remote") != NULL, "the zap shader not "
	    "resumed: \"%s\"", tz_trail());

	ibn = 0;
	ib_mem_write(&cmd, tgt.iova, 0x4242, 1);
	check(job(a, USER_A, &cmd, &tgt, &f) == OK, "a job after");
	deliver();
	check(tgt.cpu[0] == 0x4242 && wait_fence(a, USER_A, 0, f,
	    osm_mono_ns()) == OK, "the job after ran: %x", tgt.cpu[0]);

	/* The GPU's own hang detection: at once, no waiting. */
	ibn = 0;
	ib_hang(&cmd);
	check(job(a, USER_A, &cmd, NULL, &f) == OK, "another hang");
	deliver();
	cp_model_raise(A5XX_INT_MISC_HANG_DETECT);
	deliver();
	check(model_saw("up again after a hang (2 so far)"), "hang detect");
	osm_proc_exit(USER_A);
	serve_down();
}

/* A client closes with work queued and in the GPU. */
static void
t_close(void)
{
	struct cbo cmd, tgt;
	uint32_t f;
	unsigned ibs;
	int a, fa;

	printf("close\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &tgt) == OK &&
	    bo_map(a, USER_A, &cmd) == OK, "buffers");
	cp_model_defer(1);
	ibn = 0;
	ib_mem_write(&cmd, tgt.iova, 1, 1);
	check(job(a, USER_A, &cmd, &tgt, &f) == OK &&
	    job(a, USER_A, &cmd, &tgt, &f) == OK, "two jobs");
	ibs = cp_model_ibs();
	osm_proc_exit(USER_A);
	check(model_pages_live() > 0, "the buffers went under the GPU");
	gpu_go();
	gpu_go();
	check(cp_model_ibs() - ibs >= 1, "the jobs in the ring ran");
	check(model_saw("client 0 is gone"), "the client never went");
	cp_model_defer(0);
	serve_down();
	check(model_pages_live() == 0 && model_contig_live() == 0,
	    "close: %u pages, %u pieces left", model_pages_live(),
	    model_contig_live());
}

/*
 * The implicit order of a shared buffer when the first writer is held:
 * a's job waits for c's (a sync file); b's job on the buffer a writes
 * must wait for a's, not only for the ring - else it runs first.
 */
static void
t_implicit(void)
{
	struct drm_prime_handle ph;
	struct cbo ca, sh, cb, shb, cc;
	struct sub s;
	uint32_t f;
	int a, fda, b, fdb, c, fdc, fd;

	printf("implicit order\n");
	serve_up();
	a = open_ctx(USER_A, &fda);
	b = open_ctx(USER_B, &fdb);
	c = open_ctx(USER_C, &fdc);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &ca) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &sh) == OK &&
	    bo_map(a, USER_A, &ca) == OK && bo_map(a, USER_A, &sh) == OK &&
	    bo_new(b, USER_B, 4096, MSM_BO_WC, &cb) == OK &&
	    bo_map(b, USER_B, &cb) == OK &&
	    bo_new(c, USER_C, 4096, MSM_BO_WC, &cc) == OK &&
	    bo_map(c, USER_C, &cc) == OK, "buffers");
	memset(&ph, 0, sizeof(ph));
	ph.handle = sh.handle;
	ph.flags = DRM_RDWR;
	check(io(a, DRM_IOCTL_PRIME_HANDLE_TO_FD, &ph, USER_A) == OK,
	    "export");
	fd = osm_fd_pass(USER_A, ph.fd, USER_B);
	memset(&ph, 0, sizeof(ph));
	ph.fd = fd;
	check(io(b, DRM_IOCTL_PRIME_FD_TO_HANDLE, &ph, USER_B) == OK,
	    "import");
	shb.handle = ph.handle;
	shb.size = 4096;
	{
		struct drm_msm_gem_info i;

		memset(&i, 0, sizeof(i));
		i.handle = shb.handle;
		i.info = MSM_INFO_GET_IOVA;
		check(io(b, DRM_IOCTL_MSM_GEM_INFO, &i, USER_B) == OK, "iova");
		shb.iova = i.value;
	}

	cp_model_defer(1);
	ibn = 0;
	ib_word(&cc, pm4_pkt7(CP_NOP, 0));
	sub_init(&s);
	sub_bo(&s, &cc, MSM_SUBMIT_BO_READ);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_FENCE_FD_OUT;
	check(submit(c, USER_C, &s) == OK, "c's job");
	fd = osm_fd_pass(USER_C, s.fence_fd, USER_A);

	ibn = 0;
	ib_mem_write(&ca, sh.iova, 0xaaaa, 1);
	sub_init(&s);
	sub_bo(&s, &ca, MSM_SUBMIT_BO_READ);
	sub_bo(&s, &sh, MSM_SUBMIT_BO_WRITE);
	sub_cmd(&s, 0, 0, ibn, MSM_SUBMIT_CMD_BUF);
	s.flags = MSM_SUBMIT_FENCE_FD_IN;
	s.fence_fd = fd;
	check(submit(a, USER_A, &s) == OK, "a's job, after c's");
	ibn = 0;
	ib_mem_write(&cb, shb.iova, 0xbbbb, 1);
	check(job(b, USER_B, &cb, &shb, &f) == OK, "b's job on the buffer");
	gpu_go();
	gpu_go();
	gpu_go();
	gpu_go();
	check(sh.cpu[0] == 0xbbbb, "the buffer holds %x: b's job ran before "
	    "a's", sh.cpu[0]);
	cp_model_defer(0);
	osm_proc_exit(USER_C);
	osm_proc_exit(USER_B);
	osm_proc_exit(USER_A);
	serve_down();
}

/* Going down with work in the GPU and a wait asleep. */
static void
t_down_busy(void)
{
	struct cbo cmd, tgt;
	uint32_t f;
	cdev_id_t w;
	char why[160];
	int a, fa;

	printf("down, busy\n");
	serve_up();
	a = open_ctx(USER_A, &fa);
	check(bo_new(a, USER_A, 4096, MSM_BO_WC, &cmd) == OK &&
	    bo_new(a, USER_A, 4096, MSM_BO_WC, &tgt) == OK &&
	    bo_map(a, USER_A, &cmd) == OK, "buffers");
	cp_model_defer(1);
	ibn = 0;
	ib_mem_write(&cmd, tgt.iova, 1, 1);
	check(job(a, USER_A, &cmd, &tgt, &f) == OK, "a job");
	check(wait_fence(a, USER_A, 0, f, osm_mono_ns() + 5000000000LL) ==
	    EDONTREPLY, "a wait");
	w = last_id;
	serve_down();
	/* Its job failed under it, then the service went: answered either
	 * way, never left asleep. */
	check(reply_of(w) == OK || reply_of(w) == EIO, "the wait at going "
	    "down: %d", reply_of(w));
	cp_model_defer(0);
	osm_proc_exit(USER_A);
	check(model_same_as_start(why, sizeof(why)), "down: %s", why);
	check(model_pages_live() == 0, "down: %u pages left",
	    model_pages_live());
}

static void *
load(const char *path)
{
	FILE *f = fopen(path, "rb");
	long n;
	void *buf;

	if (f == NULL || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0) {
		fprintf(stderr, "msmtest: cannot read %s\n", path);
		exit(2);
	}
	rewind(f);
	if ((buf = malloc((size_t)n)) == NULL ||
	    fread(buf, 1, (size_t)n, f) != (size_t)n) {
		fprintf(stderr, "msmtest: cannot read %s\n", path);
		exit(2);
	}
	fclose(f);
	return buf;
}

int
main(int argc, char **argv)
{
	char dir[] = "/tmp/msmtest.XXXXXX";
	int a = 1;

	/* A line at a time: a FAIL written just before a crash must reach the
	 * file mutate.sh reads. */
	setvbuf(stdout, NULL, _IOLBF, 0);
	if (argc > 1 && strcmp(argv[1], "-v") == 0) {
		model_verbose = 1;
		a = 2;
	}
	if (argc - a != 1) {
		fprintf(stderr, "usage: msmtest [-v] CLOVER_DTB\n");
		return 2;
	}
	dtb = load(argv[a]);
	if (mkdtemp(dir) == NULL) {
		fprintf(stderr, "msmtest: mkdtemp\n");
		return 2;
	}
	make_files(dir);
	strlcpy(ad_fwdir, dir, sizeof(ad_fwdir));

	if (setjmp(model_hang) != 0) {
		printf("  FAIL: the model stopped the run: %s\n",
		    model_violation());
		failures++;
	} else {
		t_start();
		t_clock();
		t_params();
		t_buffers();
		t_job();
		t_waits();
		t_queues();
		t_submit_errors();
		t_fault();
		t_two();
		t_prime();
		t_fences();
		t_implicit();
		t_hang();
		t_close();
		t_down_busy();
		t_gov();
	}
	if (osm_errors != 0) {
		printf("  FAIL: %u errors of the environment (last: %s)\n",
		    osm_errors, osm_error_text);
		failures++;
	}
	printf("msm: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}
