/*
 * Модель SoC вокруг Adreno 512 (model.h).
 *
 * Что модель знает о железе — то, что сказано в port/clover/hw-gpu.md, §2,
 * §4 и §6, и ничего сверх. Правила строже железа там, где железо не
 * описано: обращение к GPUCC без GCC_GPU_CFG_AHB_CLK считается
 * зависанием, хотя Linux этого не утверждает; служба держится того же
 * предположения, и стенд проверяет, что она его держится.
 *
 *   GCC     всегда доступен. Бит 0 CBCR включает ветвь, CLK_OFF (31)
 *           гаснет через 2 мкс.
 *   GPUCC   только при включённом CFG_AHB.
 *           CX: снятый SW_COLLAPSE (0x1004, бит 0) даёт PWR_ON в 0x1008
 *           через 5 мкс.
 *           GX: снятый SW_COLLAPSE (0x1094) даёт PWR_ON в 0x1094 через
 *           20 мкс, только если CX включён и I/O не зажат (0x130, бит 0).
 *           Включение GX при выключенном CX — нарушение.
 *           Ветвь включается, только если её корень стоит на работающем
 *           источнике (кварц или захваченный PLL) и её домен включён:
 *           gfx3d — в GX, rbbmtimer и rbcpr — в CX.
 *           Корень принимает CFG только по UPDATE (CMD, бит 0), UPDATE
 *           сходит через 1 мкс.
 *   PLL     GPU (0x00, 0x40; hw-gpu.md, §2 и §20): захват (LOCK_DET)
 *           через 20 мкс после RESET_N, если BYPASSNL стоит не меньше
 *           5 мкс, конфигурация — Linux'а (0x4001055b), а частота — в
 *           полосе VCO из USER_CTL; иначе захвата нет.  Работает — захват,
 *           OUTCTRL и PLLOUT_MAIN.  Запись L, ALPHA, USER_CTL или
 *           CONFIG_CTL при поднятом RESET_N — нарушение (у этого PLL нет
 *           динамической смены частоты).  Остановить PLL, от которого идёт
 *           работающий такт ядра, — нарушение.
 *   Ядро    gfx3d: корень работающей ветви переводится только на
 *           работающий источник; частота — из таблицы Linux
 *           (ftbl_gfx3d_clk_src_660) и не выше того, что держит VDDMX:
 *           загрузчик оставляет MIN_SVS, это 160 МГц, а голос службы
 *           (ad_rpm_set(), здесь вместо qcom_rpm) — уровень по таблице
 *           sdm660.dtsi.  Голос, снятый под работающим ядром, которое
 *           уровень больше не держит, — нарушение; голос, не снятый к
 *           концу, — «не как было».
 *   qfprom  0x780000: фьюз бина в 0x41a2, биты 5..12 — из сценария.
 *   GPU     только при CFG_AHB, обеих BIMC, CX, GX и такте gfx3d.
 *           ALWAYSON_COUNTER идёт от времени модели на 19,2 МГц,
 *           CP_SCRATCH хранит записанное.
 *   SMMU    только при CFG_AHB, обеих BIMC, CX и GX; что внутри —
 *           smmumodel.c (регистры, таблицы, TLB, линия банка 0).
 *
 * Время — только micro_delay() службы; отложенные изменения применяются
 * при каждом обращении и каждой задержке.
 */

#include "adreno.h"
#include "adreno_regs.h"
#include "model.h"

#include <minix/qcomscm.h>
#include <minix/qcom_rpm.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

jmp_buf model_hang;
int model_verbose;

/* Addresses as clover's tree gives them; checked against it by adrtest. */
#define GCC_PA		0x00100000UL
#define GCC_SZ		0x94000
#define GPUCC_PA	0x05065000UL
#define GPUCC_SZ	0x9038
#define GPU_PA		0x05000000UL
#define GPU_SZ		0x40000
#define SMMU_PA		0x05040000UL
#define SMMU_SZ		0x10000
#define QFPROM_PA	0x00780000UL
#define QFPROM_SZ	0x7000

static uint8_t gcc[GCC_SZ] __attribute__((aligned(8)));
static uint8_t gpucc[GPUCC_SZ + 8] __attribute__((aligned(8)));
static uint8_t gpu[GPU_SZ] __attribute__((aligned(8)));
static uint8_t qfprom[QFPROM_SZ] __attribute__((aligned(8)));
#define smmu		smmu_window()	/* smmumodel.c: registers, tables */

/* GCC */
#define G_IFACE		0x71004
#define G_BIMC		0x7106c
#define G_GPU_BIMC	0x71010
#define G_VOTE		0x5200c
/* GPUCC */
#define P_CX		0x1004
#define P_CX_ST		0x1008
#define P_GX		0x1094
#define P_CLAMP		0x0130
#define P_GX_BCR	0x1090
#define P_CXO		0x1020
#define P_RBCPR		0x1054
#define P_GFX3D		0x1098
#define P_RBBM		0x10d0
#define R_GFX3D		0x1070
#define R_RBCPR		0x1030
#define R_RBBM		0x10b0
#define PLL0		0x0000
#define PLL1		0x0040

static const struct scenario *sc;
static unsigned long long now_us;
static unsigned writes;
static char violation[256];
static int bcr_pulsed;
static unsigned long long bcr_since;
static uint32_t active_cfg[3];		/* gfx3d, rbcpr, rbbm roots */
static const uint32_t rcgs[3] = { R_GFX3D, R_RBCPR, R_RBBM };
static unsigned long long pll_bypass_at[2];
static unsigned mx_max_mhz;		/* what VDDMX holds: MIN_SVS, 160 */
static int rpm_on;			/* the service's "swen" */
static unsigned rpm_level;		/* its "vlvl" */

static struct pend {
	uint8_t *mem;
	uint32_t off, mask, val;
	unsigned long long at;
	int used;
} pend[32];

/* The start, for model_same_as_start(). */
static struct {
	uint32_t gcc_bits[4];
	uint32_t p_bits[8];
	uint32_t cfg[3];
	uint32_t pll[2][6];
} start;

static char logbuf[1 << 16];
static size_t loglen;

static uint32_t *
w32(uint8_t *mem, uint32_t off)
{
	return (uint32_t *)(void *)(mem + off);
}

static void
apply_pending(void)
{
	unsigned i;

	for (i = 0; i < sizeof(pend) / sizeof(pend[0]); i++)
		if (pend[i].used && now_us >= pend[i].at) {
			*w32(pend[i].mem, pend[i].off) =
			    (*w32(pend[i].mem, pend[i].off) & ~pend[i].mask) |
			    pend[i].val;
			pend[i].used = 0;
		}
}

static void
later(uint8_t *mem, uint32_t off, uint32_t mask, uint32_t val, unsigned us)
{
	unsigned i;

	/* A newer change to the same bits replaces an older one. */
	for (i = 0; i < sizeof(pend) / sizeof(pend[0]); i++)
		if (pend[i].used && pend[i].mem == mem && pend[i].off == off &&
		    pend[i].mask == mask)
			pend[i].used = 0;
	for (i = 0; i < sizeof(pend) / sizeof(pend[0]); i++)
		if (!pend[i].used) {
			pend[i] = (struct pend){ mem, off, mask, val,
			    now_us + us, 1 };
			return;
		}
	fprintf(stderr, "model: too many pending changes\n");
	abort();
}

static void
hang(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(violation, sizeof(violation), fmt, ap);
	va_end(ap);
	longjmp(model_hang, 1);
}

/* The same, for smmumodel.c: a violation ends the scenario. */
void
model_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(violation, sizeof(violation), fmt, ap);
	va_end(ap);
	longjmp(model_hang, 1);
}

void
micro_delay(unsigned long us)
{
	now_us += us;
	apply_pending();
}

unsigned long long
model_now_us(void)
{
	return now_us;
}

static int gcc_on(uint32_t off) { return !(*w32(gcc, off) & CBCR_OFF); }
static int cx_on(void) { return (*w32(gpucc, P_CX_ST) & GDSC_PWR_ON) != 0; }
static int gx_on(void) { return (*w32(gpucc, P_GX) & GDSC_PWR_ON) != 0; }
static int br_on(uint32_t off) { return !(*w32(gpucc, off) & CBCR_OFF); }

static const char *
bus_off(int which)
{
	if (!gcc_on(G_IFACE))
		return "GCC_GPU_CFG_AHB_CLK";
	if (which == 0)
		return NULL;			/* the GPUCC: its bus only */
	if (!gcc_on(G_BIMC))
		return "GCC_BIMC_GFX_CLK";
	if (!gcc_on(G_GPU_BIMC))
		return "GCC_GPU_BIMC_GFX_CLK";
	if (!cx_on())
		return "the CX domain";
	if (!gx_on())
		return "the GX domain";
	if (which == 2 && !br_on(P_GFX3D))
		return "gpucc_gfx3d_clk";
	return NULL;
}

static uint32_t
pll_khz_of(uint32_t base)
{
	uint64_t l = *w32(gpucc, base + PLL_L);
	uint64_t a = *w32(gpucc, base + PLL_ALPHA) |
	    (uint64_t)(*w32(gpucc, base + PLL_ALPHA_U) & 0xff) << 32;
	uint64_t khz = 19200 * l;

	if (*w32(gpucc, base + PLL_USER_CTL) & PLL_USER_ALPHA_EN)
		khz += (19200 * a) >> 40;
	return (uint32_t)khz;
}

/* Would it lock: Linux's configuration, the rate in its VCO band. */
static int
pll_lockable(uint32_t base)
{
	uint32_t khz = pll_khz_of(base);
	unsigned band = (*w32(gpucc, base + PLL_USER_CTL) >> 20) & 3;

	if (*w32(gpucc, base + PLL_CONFIG_CTL) != PLL_CONFIG_GPU)
		return 0;
	switch (band) {
	case 0: return khz >= 1000000 && khz <= 2000000;
	case 2: return khz >= 500000 && khz <= 1000000;
	case 3: return khz >= 250000 && khz <= 500000;
	}
	return 0;
}

static int
pll_runs(uint32_t base)
{
	uint32_t m = *w32(gpucc, base + PLL_MODE);

	return (m & PLL_MODE_LOCK_DET) && (m & PLL_MODE_OUTCTRL) &&
	    (*w32(gpucc, base + PLL_USER_CTL) & PLL_USER_MAIN);
}

static uint32_t
core_khz_of(uint32_t cfg)
{
	unsigned src = RCG_CFG_SRC(cfg), hid = RCG_CFG_HID(cfg);
	uint32_t in = src == 0 ? 19200 : src == 1 ? pll_khz_of(PLL0) :
	    src == 3 ? pll_khz_of(PLL1) : 0;

	return hid == 0 ? in : (uint32_t)((uint64_t)in * 2 / (hid + 1));
}

uint32_t
model_core_khz(void)
{
	return core_khz_of(active_cfg[0]);
}

/* Is the core's branch running? (enabled, its clock coming) */
static int
core_running(void)
{
	return (*w32(gpucc, P_GFX3D) & CBCR_ENABLE) && br_on(P_GFX3D);
}

/* The core's clock as it now runs: a rate of the table, VDDMX enough. */
static void
core_check(void)
{
	static const unsigned ok[] = { 19200, 160000, 266000, 370000, 430000,
	    465000, 585000, 588000, 647000, 700000, 750000 };
	uint32_t k = model_core_khz();
	unsigned i;

	if (!(*w32(gpucc, P_GFX3D) & CBCR_ENABLE))
		return;
	for (i = 0; i < sizeof(ok) / sizeof(ok[0]); i++)
		if (k + 500 >= ok[i] && k <= ok[i] + 500)
			break;
	if (i == sizeof(ok) / sizeof(ok[0]))
		hang("gfx3d at %u kHz: not a rate of Linux's table", k);
	if (k > mx_max_mhz * 1000 + 500)
		hang("gfx3d at %u MHz with VDDMX holding %u MHz", k / 1000,
		    mx_max_mhz);
}

/* The rate a VDDMX level holds (sdm660.dtsi's points); the boot's
 * MIN_SVS at least, whatever is voted. */
static unsigned
level_max_mhz(unsigned level)
{
	static const struct { unsigned level, mhz; } t[] = {
		{ 48, 160 }, { 64, 266 }, { 128, 370 }, { 192, 465 },
		{ 256, 588 }, { 320, 647 }, { 384, 750 },
	};
	unsigned i, m = 160;

	for (i = 0; i < sizeof(t) / sizeof(t[0]); i++)
		if (level >= t[i].level)
			m = t[i].mhz;
	return m;
}

/* qcom_rpm, as the service's adreno_rpm.c would reach it. */
int
ad_rpm_set(uint32_t key, uint32_t value)
{
	unsigned before = mx_max_mhz;

	if (sc->no_rpm)
		return ENODEV;
	if (key == QRPM_KEY_ENABLE)
		rpm_on = value != 0;
	else if (key == QRPM_KEY_LEVEL)
		rpm_level = value;
	else
		model_fail("a key qcom_rpm does not take: %08x", key);
	mx_max_mhz = rpm_on ? level_max_mhz(rpm_level) : 160;
	if (mx_max_mhz < before && core_running() &&
	    model_core_khz() > mx_max_mhz * 1000 + 500)
		hang("VDDMX down to hold %u MHz under the core at %u MHz",
		    mx_max_mhz, model_core_khz() / 1000);
	return OK;
}

unsigned model_rpm_level(void) { return rpm_on ? rpm_level : 0; }
int model_rpm_on(void) { return rpm_on; }

/* Is the root that feeds a branch running? (a source that runs) */
static int
root_runs(uint32_t rcg)
{
	unsigned i, src;

	for (i = 0; i < 3; i++)
		if (rcgs[i] == rcg)
			break;
	src = RCG_CFG_SRC(active_cfg[i]);
	if (src == 0)
		return 1;			/* the crystal */
	if (rcg == R_GFX3D && (src == 1 || src == 3))	/* pll0, pll1 */
		return pll_runs(src == 1 ? PLL0 : PLL1);
	return 0;	/* GPLL0's branches: off in every scenario here */
}

static void
gpucc_write(uint32_t off, uint32_t v)
{
	uint32_t old = *w32(gpucc, off), domain_ok;
	unsigned i;

	*w32(gpucc, off) = v;
	switch (off) {
	case PLL0 + PLL_MODE:
	case PLL1 + PLL_MODE: {
		uint32_t base = off - PLL_MODE;
		int n = base == PLL0 ? 0 : 1;
		unsigned src = RCG_CFG_SRC(active_cfg[0]);

		/* LOCK_DET is the hardware's. */
		*w32(gpucc, off) = (v & ~PLL_MODE_LOCK_DET) |
		    (old & PLL_MODE_LOCK_DET);
		if (!(old & PLL_MODE_BYPASSNL) && (v & PLL_MODE_BYPASSNL))
			pll_bypass_at[n] = now_us;
		if (!(old & PLL_MODE_RESET_N) && (v & PLL_MODE_RESET_N)) {
			if (!(v & PLL_MODE_BYPASSNL))
				hang("pll%d: reset released with the bypass "
				    "still on", n);
			if (now_us < pll_bypass_at[n] + 5)
				hang("pll%d: reset released %llu us after "
				    "the bypass, not 5", n,
				    now_us - pll_bypass_at[n]);
			if (pll_lockable(base))
				later(gpucc, off, PLL_MODE_LOCK_DET,
				    PLL_MODE_LOCK_DET, 20);
		}
		if ((old & PLL_MODE_RESET_N) && !(v & PLL_MODE_RESET_N)) {
			later(gpucc, off, PLL_MODE_LOCK_DET, 0, 0);
			*w32(gpucc, off) &= ~PLL_MODE_LOCK_DET;
		}
		if (((old & PLL_MODE_OUTCTRL) && !(v & PLL_MODE_OUTCTRL)) ||
		    ((old & PLL_MODE_RESET_N) && !(v & PLL_MODE_RESET_N)))
			if (core_running() && src == (n == 0 ? 1u : 3u))
				hang("pll%d stopped under the running core "
				    "clock", n);
		break;
	}
	case PLL0 + PLL_L: case PLL0 + PLL_ALPHA: case PLL0 + PLL_ALPHA_U:
	case PLL0 + PLL_USER_CTL: case PLL0 + PLL_CONFIG_CTL:
	case PLL1 + PLL_L: case PLL1 + PLL_ALPHA: case PLL1 + PLL_ALPHA_U:
	case PLL1 + PLL_USER_CTL: case PLL1 + PLL_CONFIG_CTL:
		if (*w32(gpucc, (off & 0x40) + PLL_MODE) & PLL_MODE_RESET_N)
			hang("pll%d: register %02x written while it runs (no "
			    "dynamic update)", (off & 0x40) ? 1 : 0, off & 0x3f);
		break;
	case P_CX:
		if ((old & GDSC_SW_COLLAPSE) && !(v & GDSC_SW_COLLAPSE))
			later(gpucc, P_CX_ST, GDSC_PWR_ON, GDSC_PWR_ON, 5);
		else if (!(old & GDSC_SW_COLLAPSE) && (v & GDSC_SW_COLLAPSE)) {
			if (gx_on())
				hang("CX collapsed with GX on");
			later(gpucc, P_CX_ST, GDSC_PWR_ON, 0, 5);
		}
		break;
	case P_GX:
		/* PWR_ON is the hardware's: a write does not set it. */
		*w32(gpucc, off) = (v & ~GDSC_PWR_ON) | (old & GDSC_PWR_ON);
		if ((old & GDSC_SW_COLLAPSE) && !(v & GDSC_SW_COLLAPSE)) {
			if (!cx_on())
				hang("GX switched on with CX off");
			if (!sc->gx_dead && !(*w32(gpucc, P_CLAMP) & CLAMP_IO))
				later(gpucc, P_GX, GDSC_PWR_ON, GDSC_PWR_ON,
				    20);
		} else if (!(old & GDSC_SW_COLLAPSE) &&
		    (v & GDSC_SW_COLLAPSE)) {
			cp_model_gx_off();
			smmu_model_gx_off();
			later(gpucc, P_GX, GDSC_PWR_ON, 0, 20);
			bcr_pulsed = 0;
		}
		break;
	case P_GX_BCR:
		if (!(old & BCR_ASSERT) && (v & BCR_ASSERT))
			bcr_since = now_us;
		else if ((old & BCR_ASSERT) && !(v & BCR_ASSERT) &&
		    now_us >= bcr_since + 1)
			bcr_pulsed = 1;
		break;
	case P_CXO:
	case P_RBCPR:
	case P_GFX3D:
	case P_RBBM:
		/* CLK_OFF is the hardware's. */
		*w32(gpucc, off) = (v & ~CBCR_OFF) | (old & CBCR_OFF);
		if (!(old & CBCR_ENABLE) && (v & CBCR_ENABLE)) {
			domain_ok = off == P_GFX3D ? gx_on() :
			    off == P_CXO ? 1 : cx_on();
			if (domain_ok && (off == P_CXO || root_runs(
			    off == P_GFX3D ? R_GFX3D : off == P_RBCPR ?
			    R_RBCPR : R_RBBM)))
				later(gpucc, off, CBCR_OFF, 0, 2);
		} else if ((old & CBCR_ENABLE) && !(v & CBCR_ENABLE))
			*w32(gpucc, off) |= CBCR_OFF;
		break;
	case R_GFX3D:
	case R_RBCPR:
	case R_RBBM:
		if (v & RCG_CMD_UPDATE) {
			for (i = 0; i < 3; i++)
				if (rcgs[i] == off)
					break;
			if (sc->root_stuck && off == R_GFX3D)
				break;		/* never done */
			if (off == R_GFX3D && core_running()) {
				uint32_t ncfg = *w32(gpucc, off + 4);
				unsigned ns = RCG_CFG_SRC(ncfg);

				if (ns != 0 && !pll_runs(ns == 1 ? PLL0 :
				    ns == 3 ? PLL1 : 0x80))
					hang("gfx3d's root moved to source %u, "
					    "which does not run, under the "
					    "running core", ns);
			}
			active_cfg[i] = *w32(gpucc, off + 4);
			later(gpucc, off, RCG_CMD_UPDATE, 0, 1);
			if (off == R_GFX3D)
				core_check();
		}
		break;
	}
}

static void
gcc_write(uint32_t off, uint32_t v)
{
	uint32_t old = *w32(gcc, off);

	*w32(gcc, off) = v;
	if (off == G_IFACE || off == G_BIMC || off == G_GPU_BIMC) {
		*w32(gcc, off) = (v & ~CBCR_OFF) | (old & CBCR_OFF);
		if (!(old & CBCR_ENABLE) && (v & CBCR_ENABLE))
			later(gcc, off, CBCR_OFF, 0, 2);
		else if ((old & CBCR_ENABLE) && !(v & CBCR_ENABLE))
			*w32(gcc, off) |= CBCR_OFF;
	}
}

static uint32_t
gpu_read(uint32_t off)
{
	unsigned long long ticks = now_us * 192 / 10;
	uint32_t v;

	if (cp_model_rd(off / 4, &v))
		return v;
	if (off == A5XX(A5XX_RBBM_ALWAYSON_COUNTER_LO))
		return (uint32_t)ticks;
	if (off == A5XX(A5XX_RBBM_ALWAYSON_COUNTER_HI))
		return (uint32_t)(ticks >> 32);
	return *w32(gpu, off);
}

uint32_t
model_rd(const struct ad_region *r, uint32_t off)
{
	const char *off_one;

	apply_pending();
	if (off + 4 > r->size)
		hang("%s +0x%x: outside the window (0x%zx)", r->name, off,
		    r->size);
	if (r->base == gcc)
		return *w32(gcc, off);
	if (r->base >= qfprom && r->base < qfprom + QFPROM_SZ)
		return *(const uint32_t *)(const void *)(r->base + off);
	if (r->base == gpucc) {
		if ((off_one = bus_off(0)) != NULL)
			hang("read of gpucc +0x%x with %s off", off, off_one);
		return *w32(gpucc, off);
	}
	if (r->base == gpu) {
		if ((off_one = bus_off(2)) != NULL)
			hang("read of gpu +0x%x with %s off", off, off_one);
		return gpu_read(off);
	}
	if (r->base == smmu) {
		if ((off_one = bus_off(1)) != NULL)
			hang("read of smmu +0x%x with %s off", off, off_one);
		return smmu_rd(off);
	}
	hang("read of an unknown region %s", r->name);
	return 0;
}

uint64_t
model_rd64(const struct ad_region *r, uint32_t off)
{
	const char *off_one;

	apply_pending();
	if (off + 8 > r->size || r->base != smmu)
		hang("%s +0x%x: a 64-bit read the model does not know", r->name,
		    off);
	if ((off_one = bus_off(1)) != NULL)
		hang("read of smmu +0x%x with %s off", off, off_one);
	return smmu_rd64(off);
}

void
model_wr64(const struct ad_region *r, uint32_t off, uint64_t v)
{
	const char *off_one;

	apply_pending();
	writes++;
	if (off + 8 > r->size || r->base != smmu)
		hang("%s +0x%x: a 64-bit write the model does not know", r->name,
		    off);
	if ((off_one = bus_off(1)) != NULL)
		hang("write of smmu +0x%x with %s off", off, off_one);
	smmu_wr64(off, v);
}

void
model_wr(const struct ad_region *r, uint32_t off, uint32_t v)
{
	const char *off_one;

	apply_pending();
	writes++;
	if (off + 4 > r->size)
		hang("%s +0x%x: write outside the window", r->name, off);
	if (r->base == gcc) {
		gcc_write(off, v);
		return;
	}
	if (r->base == gpucc) {
		if ((off_one = bus_off(0)) != NULL)
			hang("write of gpucc +0x%x with %s off", off, off_one);
		gpucc_write(off, v);
		return;
	}
	if (r->base == gpu) {
		if ((off_one = bus_off(2)) != NULL)
			hang("write of gpu +0x%x with %s off", off, off_one);
		*w32(gpu, off) = v;
		cp_model_wr(off / 4, v);
		return;
	}
	if (r->base == smmu) {
		if ((off_one = bus_off(1)) != NULL)
			hang("write of smmu +0x%x with %s off", off, off_one);
		smmu_wr(off, v);
		return;
	}
	hang("write of an unknown region %s", r->name);
}

void *
vm_map_phys(int who, void *phys, size_t len)
{
	unsigned long pa = (unsigned long)phys;

	(void)who;
	if (pa == GCC_PA && len <= GCC_SZ)
		return gcc;
	if (pa == GPUCC_PA && len <= GPUCC_SZ)
		return gpucc;
	if (pa == GPU_PA && len <= GPU_SZ)
		return gpu;
	if (pa == SMMU_PA && len <= SMMU_SZ)
		return smmu;
	if (pa >= QFPROM_PA && pa + len <= QFPROM_PA + QFPROM_SZ)
		return qfprom + (pa - QFPROM_PA);
	return tz_map(pa, len);		/* the zap shader's region, or none */
}

/* The GPU's registers by dword, for cpmodel.c and the stand's checks. */
uint32_t
model_gpu_reg(uint32_t dw)
{
	return *w32(gpu, A5XX(dw));
}

void
model_gpu_set(uint32_t dw, uint32_t v)
{
	*w32(gpu, A5XX(dw)) = v;
}

/* Whether the GPU could be reached now: TrustZone's calls about it want
 * that (tzmodel.c). */
int
model_gpu_powered(void)
{
	apply_pending();
	return bus_off(2) == NULL;
}

void
stub_log(int level, const char *tag, const struct log *l, const char *fmt,
	...)
{
	char line[1024];
	va_list ap;
	int n;

	if (level > l->log_level)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	n = snprintf(logbuf + loglen, sizeof(logbuf) - loglen, "%s(%s)%s",
	    l->name, tag, line);
	if (n > 0 && (size_t)n < sizeof(logbuf) - loglen)
		loglen += (size_t)n;
	if (model_verbose)
		printf("    | %s(%s)%s", l->name, tag, line);
}

void
model_log_reset(void)
{
	loglen = 0;
	logbuf[0] = '\0';
}

int
model_saw(const char *needle)
{
	return strstr(logbuf, needle) != NULL;
}

const char *
model_violation(void)
{
	return violation;
}

unsigned
model_writes(void)
{
	return writes;
}

void
model_settle(void)
{
	micro_delay(10000);
}

static void
snapshot(uint32_t *gb, uint32_t *pb, uint32_t *cfg)
{
	unsigned i;

	gb[0] = *w32(gcc, G_IFACE) & (CBCR_ENABLE | CBCR_OFF);
	gb[1] = *w32(gcc, G_BIMC) & (CBCR_ENABLE | CBCR_OFF);
	gb[2] = *w32(gcc, G_GPU_BIMC) & (CBCR_ENABLE | CBCR_OFF);
	gb[3] = *w32(gcc, G_VOTE);
	pb[0] = (*w32(gpucc, P_CX) & GDSC_SW_COLLAPSE) |
	    (*w32(gpucc, P_CX_ST) & GDSC_PWR_ON);
	pb[1] = *w32(gpucc, P_GX) & (GDSC_SW_COLLAPSE | GDSC_PWR_ON);
	pb[2] = *w32(gpucc, P_CLAMP) & (CLAMP_IO | CLAMP_GMEM_RESET);
	pb[3] = *w32(gpucc, P_GX_BCR) & BCR_ASSERT;
	pb[4] = *w32(gpucc, P_CXO) & (CBCR_ENABLE | CBCR_OFF);
	pb[5] = *w32(gpucc, P_RBCPR) & (CBCR_ENABLE | CBCR_OFF);
	pb[6] = *w32(gpucc, P_GFX3D) & (CBCR_ENABLE | CBCR_OFF |
	    CBCR_FORCE_MEM_CORE_ON);
	pb[7] = *w32(gpucc, P_RBBM) & (CBCR_ENABLE | CBCR_OFF);
	for (i = 0; i < 3; i++)
		cfg[i] = active_cfg[i] & RCG_CFG_FIELDS;
}

static void
pll_snapshot(uint32_t pl[2][6])
{
	static const uint32_t reg[6] = { PLL_MODE, PLL_L, PLL_ALPHA,
	    PLL_ALPHA_U, PLL_USER_CTL, PLL_CONFIG_CTL };
	unsigned n, k;

	for (n = 0; n < 2; n++)
		for (k = 0; k < 6; k++)
			pl[n][k] = *w32(gpucc, (n ? PLL1 : PLL0) + reg[k]) &
			    (k == 0 ? (PLL_MODE_OUTCTRL | PLL_MODE_RESET_N |
			    PLL_MODE_BYPASSNL) : ~0u);
}

int
model_same_as_start(char *why, size_t size)
{
	static const char *const gname[4] = { "iface", "bimc", "gpu_bimc",
	    "vote" };
	static const char *const pname[8] = { "cx", "gx", "clamp", "gx_bcr",
	    "cxo", "rbcpr", "gfx3d", "rbbmtimer" };
	uint32_t gb[4], pb[8], cfg[3];
	unsigned i;

	model_settle();
	snapshot(gb, pb, cfg);
	for (i = 0; i < 4; i++)
		if (gb[i] != start.gcc_bits[i]) {
			snprintf(why, size, "gcc %s %08x, was %08x", gname[i],
			    gb[i], start.gcc_bits[i]);
			return 0;
		}
	for (i = 0; i < 8; i++)
		if (pb[i] != start.p_bits[i]) {
			snprintf(why, size, "gpucc %s %08x, was %08x",
			    pname[i], pb[i], start.p_bits[i]);
			return 0;
		}
	for (i = 0; i < 3; i++)
		if (cfg[i] != start.cfg[i]) {
			snprintf(why, size, "root %x cfg %08x, was %08x",
			    rcgs[i], cfg[i], start.cfg[i]);
			return 0;
		}
	{
		uint32_t pl[2][6];
		unsigned n, k;

		pll_snapshot(pl);
		for (n = 0; n < 2; n++)
			for (k = 0; k < 6; k++)
				if (pl[n][k] != start.pll[n][k]) {
					snprintf(why, size, "pll%u register %u "
					    "%08x, was %08x", n, k, pl[n][k],
					    start.pll[n][k]);
					return 0;
				}
	}
	if (rpm_on) {
		snprintf(why, size, "VDDMX still voted (level %u)", rpm_level);
		return 0;
	}
	return tz_model_same(why, size) && smmu_model_same(why, size);
}

void
model_reset(const struct scenario *s)
{
	unsigned i;

	sc = s;
	now_us = 1000000;
	writes = 0;
	violation[0] = '\0';
	bcr_pulsed = 0;
	memset(pend, 0, sizeof(pend));
	memset(gcc, 0, sizeof(gcc));
	memset(gpucc, 0, sizeof(gpucc));
	memset(gpu, 0, sizeof(gpu));
	memset(qfprom, 0, sizeof(qfprom));
	qfprom[0x41a2] = (uint8_t)(s->speed_fuse << 5);
	qfprom[0x41a3] = (uint8_t)(s->speed_fuse >> 3);
	rpm_on = 0;
	rpm_level = 0;

	/* GCC, as the boot loader may leave it. */
	*w32(gcc, G_IFACE) = s->iface_on ? CBCR_ENABLE : CBCR_OFF;
	*w32(gcc, G_BIMC) = s->bimc_on ? CBCR_ENABLE : CBCR_OFF;
	*w32(gcc, G_GPU_BIMC) = s->bimc_on ? CBCR_ENABLE : CBCR_OFF;
	*w32(gcc, G_VOTE) = 0;

	/* GPUCC: the crystal's branch on; the rest by the scenario. */
	*w32(gpucc, P_CXO) = CBCR_ENABLE;
	*w32(gpucc, P_CX) = s->cx_on ? 0x00222000 : 0x00222001;
	*w32(gpucc, P_CX_ST) = s->cx_on ? GDSC_PWR_ON : 0;
	*w32(gpucc, P_GX) = s->gx_on ? (0x00222000 | GDSC_PWR_ON) : 0x00222001;
	*w32(gpucc, P_CLAMP) = s->gx_on ? 0 : CLAMP_IO;
	*w32(gpucc, P_GFX3D) = s->clocks_on ? CBCR_ENABLE |
	    CBCR_FORCE_MEM_CORE_ON : CBCR_OFF;
	*w32(gpucc, P_RBBM) = s->clocks_on ? CBCR_ENABLE : CBCR_OFF;
	*w32(gpucc, P_RBCPR) = s->clocks_on ? CBCR_ENABLE : CBCR_OFF;
	/* Roots: gfx3d on PLL0 (off, 800 MHz programmed) unless running. */
	*w32(gpucc, R_GFX3D + 4) = s->clocks_on ? 0x1 : (1u << 8) | 3;
	*w32(gpucc, R_RBBM + 4) = s->clocks_on ? 0x1 : (5u << 8) | 3;
	*w32(gpucc, R_RBCPR + 4) = s->clocks_on ? 0x1 : (5u << 8) | 3;
	for (i = 0; i < 3; i++)
		active_cfg[i] = *w32(gpucc, rcgs[i] + 4);
	*w32(gpucc, 0x00 + PLL_L) = 0x29;
	*w32(gpucc, 0x00 + PLL_ALPHA) = 0xaaaaab00;
	*w32(gpucc, 0x00 + PLL_ALPHA_U) = 0xaa;
	*w32(gpucc, 0x00 + PLL_USER_CTL) = PLL_USER_ALPHA_EN;
	*w32(gpucc, 0x40 + PLL_L) = 0x29;

	/* GPU: values of no meaning, to be seen in the lines. */
	*w32(gpu, A5XX(A5XX_RBBM_HW0)) = 0x50120000;
	*w32(gpu, A5XX(A5XX_VBIF_VERSION)) = 0x20090000;
	*w32(gpu, A5XX(A5XX_RBBM_SECVID_TRUST_CNTL)) = 0x1;
	*w32(gpu, A5XX(A5XX_CP_SCRATCH_REG(0))) = 0x12345678;

	/* SMMU: smmumodel.c, by the scenario; TrustZone: tzmodel.c. */
	smmu_model_reset(s);
	tz_model_reset(s);
	cp_model_reset(s);

	model_log_reset();
	snapshot(start.gcc_bits, start.p_bits, start.cfg);
	pll_snapshot(start.pll);
	mx_max_mhz = 160;
	pll_bypass_at[0] = pll_bypass_at[1] = 0;
}
