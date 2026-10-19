/*
 * The model behind the qcom_cpufreq stand (model.h).
 *
 * Blocks by physical address, as the tablet's tree and
 * sdm660-xiaomi-clover-cpu.dtsi place them; vm_map_phys() hands out the
 * model's memory for them.  The service's register accesses come here
 * through the shim (shim/regs-hook.h); the SPMI arbiter is modelled at
 * spmi.c's interface (spmi_init/read/write) - spmi.c itself has its own
 * stand, port/test/qcom_pmic.
 *
 * The rules (port/clover/hw-cpu.md, sections 3, 8, 9):
 *   mux    a CFG takes effect at UPDATE, which clears 2 us later - only if
 *          the source runs: aux with its root on, the PLL locked with its
 *          output on.  Onto anything else it is a violation, and the switch
 *          never finishes.  The PLL's main output (source 3) is not path A.
 *   PLL    MODE 0 -> L -> MODE 2 -> at least 10 us -> MODE 6 locks 15 us
 *          later if L is in the VCO's range (43 .. 0x80, or less in a
 *          scenario); MODE 7 only after the lock.  No MODE or L while the
 *          cores run from it (or a switch to or from it is pending); L only
 *          while it is off; no other PLL register written.
 *   rate   whenever a mux finishes a switch, the rate the cores now run at
 *          must be held: by the reference table of the scenario's fuses,
 *          its corner's open-loop voltage below 872 mV and no higher than
 *          the rail's set point - S1 for silver, the lower of S2 and S3 for
 *          gold - as the PMIC holds it at that moment.
 *   else   nothing written in the OSM, CPRh, APM, SAWs, fuses, GCC, the
 *          arbiter's regions, or the PMIC.
 */

#include "qcom_cpufreq.h"
#include "model.h"

#include <stdarg.h>
#include <sys/mman.h>

int model_verbose;

/*===========================================================================*
 *    blocks                                                                 *
 *===========================================================================*/
enum kind { K_FORBID, K_GLB, K_PLL, K_FUSE, K_GCC, K_SPMI };

struct block {
	const char *name;
	uint64_t pa;
	size_t size;
	enum kind kind;
	int cl;
	uint8_t *mem, *init;
};

static struct block blocks[] = {
	{ "osm", 0x179c0000, 0x4000, K_FORBID, 0, NULL, NULL },
	{ "cprh-gold", 0x179c4000, 0x4000, K_FORBID, 0, NULL, NULL },
	{ "cprh-silver", 0x179c8000, 0x4000, K_FORBID, 0, NULL, NULL },
	{ "apcs-common (APM)", 0x179d0000, 0x2000, K_FORBID, 0, NULL, NULL },
	{ "silver-glb", 0x17911000, 0x1000, K_GLB, M_SILVER, NULL, NULL },
	{ "silver-saw", 0x17912000, 0x1000, K_FORBID, 0, NULL, NULL },
	{ "silver-pll", 0x17916000, 0x1000, K_PLL, M_SILVER, NULL, NULL },
	{ "gold-glb", 0x17811000, 0x1000, K_GLB, M_GOLD, NULL, NULL },
	{ "gold-saw", 0x17812000, 0x1000, K_FORBID, 0, NULL, NULL },
	{ "gold-pll", 0x17816000, 0x1000, K_PLL, M_GOLD, NULL, NULL },
	{ "qfprom", 0x780000, 0x7000, K_FUSE, 0, NULL, NULL },
	{ "gcc", 0x100000, 0xa0000, K_GCC, 0, NULL, NULL },
	{ "spmi-core", 0x800f000, 0x1000, K_SPMI, 0, NULL, NULL },
	{ "spmi-chnls", 0x8400000, 0x1000000, K_SPMI, 0, NULL, NULL },
	{ "spmi-obsrvr", 0x9400000, 0x1000000, K_SPMI, 0, NULL, NULL },
	{ "spmi-intr", 0xa400000, 0x220000, K_SPMI, 0, NULL, NULL },
	{ "spmi-cnfg", 0x800a000, 0x3000, K_SPMI, 0, NULL, NULL },
};
#define NBLOCK	(sizeof(blocks) / sizeof(blocks[0]))

static struct block *
block_named(const char *name)
{
	unsigned i;

	for (i = 0; i < NBLOCK; i++)
		if (strcmp(blocks[i].name, name) == 0)
			return &blocks[i];
	abort();
}

static struct block *
block_of(const volatile uint8_t *p, uint32_t *off)
{
	unsigned i;

	for (i = 0; i < NBLOCK; i++)
		if (blocks[i].mem != NULL && p >= blocks[i].mem &&
		    p < blocks[i].mem + blocks[i].size) {
			*off = (uint32_t)(p - blocks[i].mem);
			return &blocks[i];
		}
	return NULL;
}

static uint32_t
get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static void
put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/*===========================================================================*
 *    the run's state                                                        *
 *===========================================================================*/
static const struct scenario *sc;
static const struct fuse_set *fs;
static unsigned long long now;
static unsigned writes;
static char violation[512];
static unsigned violations;
static uint8_t pmic[16][0x10000];
static int held_at_start[2];

struct mux {
	uint32_t latched, active;
	int pending, stuck;
	unsigned long long t_done;
	unsigned to_pll;		/* switches to the PLL finished */
};

struct pll {
	uint32_t mode, l, user, config;
	int phase, will_lock;
	unsigned long long t_bypass, t_lock;
	char hist[64];
};

static struct mux mx[2];
static struct pll pl[2];
static const char *const clname[2] = { "silver", "gold" };

#define SRC(v)		(((v) >> 8) & 7)
#define XO_KHZ		19200
#define APM_UV		872000
#define LOCK_US		15
#define SWITCH_US	2

static void V(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void
V(const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (violations++ == 0)
		snprintf(violation, sizeof(violation), "%s", buf);
	if (model_verbose)
		printf("      model: VIOLATION %s\n", buf);
}

const char *
model_violation(void)
{
	return violations ? violation : NULL;
}

unsigned
model_writes(void)
{
	return writes;
}

/*===========================================================================*
 *    the log                                                                *
 *===========================================================================*/
struct log cf_log = { "qcom_cpufreq", LEVEL_DEBUG, NULL };

static char *logbuf;
static size_t loglen, logcap;

void
stub_log(int level, const char *tag, const struct log *l, const char *fmt,
	...)
{
	char line[512];
	va_list ap;
	size_t n;

	(void)l;
	if (level > LEVEL_DEBUG)
		return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	n = strlen(tag) + strlen(line) + 3;
	if (loglen + n + 1 > logcap) {
		logcap = (loglen + n + 1) * 2;
		if ((logbuf = realloc(logbuf, logcap)) == NULL)
			abort();
	}
	loglen += (size_t)snprintf(logbuf + loglen, logcap - loglen, "%s: %s",
	    tag, line);
	if (model_verbose > 1)
		printf("      %s: %s", tag, line);
}

void
model_log_reset(void)
{
	loglen = 0;
	if (logbuf != NULL)
		logbuf[0] = '\0';
}

int
model_saw(const char *needle)
{
	return logbuf != NULL && strstr(logbuf, needle) != NULL;
}

void
model_log_dump(void)
{
	if (logbuf != NULL)
		fputs(logbuf, stdout);
}

/*===========================================================================*
 *    the reference: what a rate needs, what the rails give                  *
 *===========================================================================*/
static unsigned
reg_mv_sid(unsigned sid, unsigned base)
{
	return (unsigned)pmic[sid][base + 0x40] |
	    (unsigned)pmic[sid][base + 0x41] << 8;
}

static unsigned
reg_mv(unsigned base)
{
	return reg_mv_sid(1, base);
}

unsigned
model_setpoint(int cl)
{
	unsigned a, b, mx = reg_mv_sid(3, 0x2000);

	/* The cluster's own rail, and VDD_MX: the arrays are on it (APM). */
	if (cl == M_SILVER)
		a = reg_mv(0x1400);
	else {
		a = reg_mv(0x1700);
		b = reg_mv(0x1a00);
		a = a < b ? a : b;
	}
	return a < mx ? a : mx;
}

static const char *
why_not_held(int cl, uint32_t khz, int32_t *uv)
{
	unsigned i;

	*uv = 0;
	if (khz == 0)
		return "no clock at all";
	if (!fs->has[cl])
		return "no corners for this chip";
	for (i = 0; i < fs->n[cl] && khz > fs->khz[cl][i]; i++)
		;
	if (i == fs->n[cl])
		return "above the table";
	*uv = fs->ol[cl][i];
	if (*uv >= APM_UV)
		return "a point at or above the APM threshold (872 mV)";
	if (*uv > (int32_t)model_setpoint(cl) * 1000)
		return "a frequency above what the set point holds";
	return NULL;
}

int
model_held(int cl, uint32_t khz)
{
	int32_t uv;

	return why_not_held(cl, khz, &uv) == NULL;
}

/*===========================================================================*
 *    the GCC's aux, the PLLs, the muxes                                     *
 *===========================================================================*/
static uint32_t
aux_khz(void)
{
	uint8_t *g = block_named("gcc")->mem;
	uint32_t cmd = get32(g + 0x4805c), cfg = get32(g + 0x48060), d;

	if ((cmd & (1u << 31)) || SRC(cfg) != 1)
		return 0;
	d = cfg & 0x1f;
	return d <= 1 ? 600000 : 600000 * 2 / (d + 1);
}

static int
pll_locked(int cl)
{
	return pl[cl].phase == 6 && pl[cl].will_lock && now >= pl[cl].t_lock;
}

static int
pll_out(int cl)
{
	return (pl[cl].mode & 7) == 7 && pll_locked(cl);
}

static uint32_t
rate_of(int cl, uint32_t cfg)
{
	uint32_t khz, d = cfg & 0x1f;

	switch (SRC(cfg)) {
	case 4:
		khz = aux_khz();
		break;
	case 3:
	case 5:
		khz = pll_out(cl) ? pl[cl].l * XO_KHZ : 0;
		break;
	default:
		khz = 0;
	}
	return d <= 1 ? khz : khz * 2 / (d + 1);
}

static void drop_rail(void);

static void
switched(int cl)
{
	const char *why;
	uint32_t khz = rate_of(cl, mx[cl].active);
	int32_t uv;

	if ((why = why_not_held(cl, khz, &uv)) != NULL)
		V("%s runs at %u kHz (open loop %d uV, set point %u mV): %s",
		    clname[cl], khz, uv, model_setpoint(cl), why);
	if (SRC(mx[cl].active) == 5 && ++mx[cl].to_pll == sc->drop_after &&
	    sc->drop_cl == cl && sc->drop_after != 0)
		drop_rail();
}

static void
mux_settle(int cl)
{
	struct mux *m = &mx[cl];

	if (m->pending && !m->stuck && now >= m->t_done) {
		m->pending = 0;
		m->active = m->latched;
		switched(cl);
	}
}

/* The cores run from the PLL, or are about to, or are leaving it. */
static int
cores_on_pll(int cl)
{
	struct mux *m = &mx[cl];

	mux_settle(cl);
	return SRC(m->active) == 3 || SRC(m->active) == 5 ||
	    (m->pending && (SRC(m->latched) == 3 || SRC(m->latched) == 5));
}

static uint32_t
glb_rd(int cl, uint32_t off, const uint8_t *mem)
{
	mux_settle(cl);
	if (off == 0x50)
		return 0x2 | (mx[cl].pending ? 1 : 0);
	if (off == 0x54)
		return mx[cl].latched;
	return get32(mem + off);
}

static void
glb_wr(int cl, uint32_t off, uint32_t v)
{
	struct mux *m = &mx[cl];
	uint32_t src;

	mux_settle(cl);
	if (off == 0x54) {
		if (m->pending)
			V("%s: CFG written while a switch is pending", clname[cl]);
		m->latched = v;
		return;
	}
	if (off != 0x50) {
		V("%s: APCS GLB register 0x%x written", clname[cl], off);
		return;
	}
	if (v & ~3u)
		V("%s: CMD 0x%x: bits beyond UPDATE and ROOT_EN", clname[cl], v);
	if (!(v & 2))
		V("%s: CMD 0x%x clears ROOT_EN", clname[cl], v);
	if (!(v & 1))
		return;
	if (m->pending) {
		V("%s: UPDATE while a switch is pending", clname[cl]);
		return;
	}
	src = SRC(m->latched);
	m->pending = 1;
	m->stuck = sc->mux_stuck[cl];
	m->t_done = now + SWITCH_US;
	if (src == 3) {
		V("%s: the mux put on the PLL's main output (not path A)",
		    clname[cl]);
		m->stuck = 1;
	} else if (src == 5 && !pll_out(cl)) {
		V("%s: the mux put on an unlocked PLL (mode 0x%x)",
		    clname[cl], pl[cl].mode);
		m->stuck = 1;
	} else if (src == 4 && aux_khz() == 0) {
		V("%s: the mux put on aux, whose root is off", clname[cl]);
		m->stuck = 1;
	} else if (src != 4 && src != 5) {
		V("%s: the mux put on source %u", clname[cl], src);
		m->stuck = 1;
	}
}

static uint32_t
pll_rd(int cl, uint32_t off, const uint8_t *mem)
{
	switch (off) {
	case 0x00: return pl[cl].mode;
	case 0x04: return pl[cl].l;
	case 0x0c: return pl[cl].user;
	case 0x10: return pl[cl].config;
	case 0x2c: return pll_locked(cl) ? 0x10000 : 0;
	default: return get32(mem + off);
	}
}

static void
pll_wr(int cl, uint32_t off, uint32_t v)
{
	struct pll *p = &pl[cl];
	uint32_t top = sc->vco_max[cl] ? sc->vco_max[cl] : 0x80;
	char b[8];

	if (cores_on_pll(cl))
		V("%s: %s written to the PLL the cores run on", clname[cl],
		    off == 0 ? "MODE" : off == 4 ? "L" : "a register");
	if (off == 0x04) {
		if (p->phase != 0 || (p->mode & 7) != 0)
			V("%s: L written to a PLL that is not off (mode 0x%x)",
			    clname[cl], p->mode);
		if (!sc->l_ro[cl])
			p->l = v;
		return;
	}
	if (off != 0x00) {
		V("%s: PLL register 0x%x written (only MODE and L are the "
		    "service's)", clname[cl], off);
		return;
	}
	switch (v) {
	case 0:
		p->phase = 0;
		p->will_lock = 0;
		break;
	case 2:
		if (p->phase != 0)
			V("%s: MODE 2 on a PLL that was not off", clname[cl]);
		p->phase = 2;
		p->t_bypass = now;
		break;
	case 6:
		if (p->phase != 2)
			V("%s: MODE 6 not after MODE 2", clname[cl]);
		p->phase = 6;
		p->will_lock = now - p->t_bypass >= 10 && p->l >= 43 &&
		    p->l <= top && !sc->pll_dead[cl];
		p->t_lock = now + LOCK_US;
		break;
	case 7:
		if (p->phase != 6)
			V("%s: MODE 7 not after MODE 6", clname[cl]);
		if (!pll_locked(cl)) {
			V("%s: the PLL's output on before it locked",
			    clname[cl]);
			break;
		}
		snprintf(b, sizeof(b), "%s%x", p->hist[0] ? " " : "", p->l);
		strncat(p->hist, b, sizeof(p->hist) - strlen(p->hist) - 1);
		if (sc->drop_l != 0 && sc->drop_cl == cl && p->l == sc->drop_l)
			drop_rail();
		break;
	default:
		V("%s: MODE 0x%x is no step of the sequence", clname[cl], v);
	}
	p->mode = v;
}

/*===========================================================================*
 *    the service's accesses                                                 *
 *===========================================================================*/
uint32_t
model_rd(const struct cf_region *r, uint32_t off)
{
	struct block *b;
	uint32_t o;

	if ((b = block_of(r->base + off, &o)) == NULL) {
		V("a read outside every block (%s +0x%x)", r->name, off);
		return 0;
	}
	if (o & 3)
		V("an unaligned read of %s +0x%x", b->name, o);
	if (b->kind == K_GLB)
		return glb_rd(b->cl, o, b->mem);
	if (b->kind == K_PLL)
		return pll_rd(b->cl, o, b->mem);
	return get32(b->mem + o);
}

void
model_wr(const struct cf_region *r, uint32_t off, uint32_t v)
{
	struct block *b;
	uint32_t o;

	writes++;
	if ((b = block_of(r->base + off, &o)) == NULL) {
		V("a write outside every block (%s +0x%x)", r->name, off);
		return;
	}
	if (model_verbose > 2)
		printf("      model: %s +0x%x = 0x%x at %llu us\n", b->name, o, v,
		    now);
	switch (b->kind) {
	case K_GLB:
		glb_wr(b->cl, o, v);
		break;
	case K_PLL:
		pll_wr(b->cl, o, v);
		break;
	default:
		V("a write to %s +0x%x (0x%x)", b->name, o, v);
		put32(b->mem + o, v);
	}
}

void *
vm_map_phys(int who, void *phys, size_t len)
{
	uint64_t pa = (uint64_t)(uintptr_t)phys;
	unsigned i;

	(void)who;
	for (i = 0; i < NBLOCK; i++)
		if (pa >= blocks[i].pa && pa + len <= blocks[i].pa +
		    blocks[i].size)
			return blocks[i].mem + (pa - blocks[i].pa);
	V("a mapping the model has no block for: 0x%llx+0x%zx",
	    (unsigned long long)pa, len);
	return MAP_FAILED;
}

int
vm_unmap_phys(int who, void *vaddr, size_t len)
{
	(void)who; (void)vaddr; (void)len;
	return 0;
}

void
cf_delay_us(unsigned us)
{
	now += us;
}

void
cf_pause(unsigned seconds)
{
	now += (unsigned long long)seconds * 1000000;
}

/*===========================================================================*
 *    the PMIC behind the arbiter                                            *
 *===========================================================================*/
/* A rail set from the stand: 0 S1, 1 S2, 2 S3, 3 MX. */
void
model_set_rail(unsigned reg, unsigned mv)
{
	static const unsigned base[4] = { 0x1400, 0x1700, 0x1a00, 0x2000 };
	unsigned sid = reg == 3 ? 3 : 1;

	pmic[sid][base[reg] + 0x40] = (uint8_t)mv;
	pmic[sid][base[reg] + 0x41] = (uint8_t)(mv >> 8);
}

/* The PLL's MODE, its low three bits. */
unsigned
model_pll_mode(int cl)
{
	return pl[cl].mode & 7;
}

/* A rate the rails do not hold is now a violation at the end too. */
void
model_hold_now(void)
{
	unsigned c;

	for (c = 0; c < 2; c++)
		held_at_start[c] = 1;
}

static void
drop_rail(void)
{
	static const unsigned base[4] = { 0x1400, 0x1700, 0x1a00, 0x2000 };
	unsigned b = base[sc->drop_reg], sid = sc->drop_reg == 3 ? 3 : 1;

	pmic[sid][b + 0x40] = (uint8_t)sc->drop_mv;
	pmic[sid][b + 0x41] = (uint8_t)(sc->drop_mv >> 8);
	if (model_verbose)
		printf("      model: %s%u drops to %u mV\n", sid == 3 ? "MX, S" :
		    "S", sc->drop_reg == 3 ? 5 : sc->drop_reg + 1, sc->drop_mv);
}

/* The tablet's channels for PM660's CPU rails (cpu-probe-20261018.txt). */
static const struct { unsigned ppid, apid; } chans[] = {
	{ 0x114, 27 }, { 0x115, 62 }, { 0x116, 18 },	/* S1 */
	{ 0x117, 53 }, { 0x118, 65 }, { 0x119, 21 },	/* S2 */
	{ 0x11a, 66 }, { 0x11b, 23 }, { 0x11c, 67 },	/* S3 */
	{ 0x320, 152 }, { 0x321, 153 }, { 0x322, 154 },	/* PM660L S5, VDD_MX */
};

int
spmi_init(struct spmi *s)
{
	unsigned i;

	if (s->core != block_named("spmi-core")->mem ||
	    s->chnls != block_named("spmi-chnls")->mem ||
	    s->obsrvr != block_named("spmi-obsrvr")->mem ||
	    s->cnfg != block_named("spmi-cnfg")->mem ||
	    s->core_size != 0x1000 || s->obsrvr_size != 0x1000000 ||
	    s->cnfg_size != 0x3000 || s->delay_us == NULL || s->ee != 0)
		V("the arbiter's regions are not the tree's");
	s->version = 0x30000000;
	s->napid = 512;
	memset(s->apid_of, 0xff, sizeof(s->apid_of));
	memset(s->owner, 0xff, sizeof(s->owner));
	for (i = 0; i < sizeof(chans) / sizeof(chans[0]); i++) {
		s->apid_of[chans[i].ppid] = (uint16_t)chans[i].apid;
		/* MX's channels are the RPM's (EE 4), the rest the processor's. */
		s->owner[chans[i].apid] = chans[i].ppid >> 8 == 3 ? 4 : 0;
	}
	return OK_SPMI;
}

int
spmi_read(struct spmi *s, unsigned sid, unsigned addr, uint8_t *buf,
	unsigned len)
{
	unsigned i;

	if (len == 0 || len > 8)
		return EINVAL;
	if (sid > 15 || addr + len > 0x10000 ||
	    s->apid_of[(sid << 8) | (addr >> 8)] == SPMI_NO_APID)
		return ENODEV;
	if (sc->spmi_dead) {
		for (i = 0; i < 1000; i++)
			s->delay_us(1);
		return ETIMEDOUT;
	}
	s->delay_us(1);
	if (sc->spmi_flaky != 0) {
		static unsigned long n;

		n++;
		if (n % sc->spmi_flaky == 0)
			return EIO;
		if (n % (sc->spmi_flaky + 3) == 0) {
			memset(buf, 0, len);
			return OK_SPMI;
		}
	}
	memcpy(buf, &pmic[sid][addr], len);
	return OK_SPMI;
}

int
spmi_write(struct spmi *s, unsigned sid, unsigned addr, const uint8_t *buf,
	unsigned len)
{
	(void)s; (void)buf; (void)len;
	writes++;
	V("a write to the PMIC: SID %u 0x%04x", sid, addr);
	return EPERM;
}

int
spmi_present(const struct spmi *s, unsigned sid, unsigned addr)
{
	return sid <= 15 && addr <= 0xffff &&
	    s->apid_of[(sid << 8) | (addr >> 8)] != SPMI_NO_APID;
}

int
spmi_writable(const struct spmi *s, unsigned sid, unsigned addr)
{
	return spmi_present(s, sid, addr) &&
	    s->owner[s->apid_of[(sid << 8) | (addr >> 8)]] == s->ee;
}

/*===========================================================================*
 *    the fuse sets (volt-ref.txt)                                           *
 *===========================================================================*/
static struct fuse_set *sets;
static unsigned nsets;

int
model_load_fuses(const char *path)
{
	char line[256], name[64];
	struct fuse_set *f = NULL;
	unsigned r, cl, bin, n, k;
	unsigned long long v;
	long khz, ol, fl;
	FILE *fp;
	int cur = -1;

	if ((fp = fopen(path, "r")) == NULL)
		return -1;
	while (fgets(line, sizeof(line), fp) != NULL) {
		if (sscanf(line, "set %63s", name) == 1) {
			sets = realloc(sets, (nsets + 1) * sizeof(*sets));
			f = &sets[nsets++];
			memset(f, 0, sizeof(*f));
			snprintf(f->name, sizeof(f->name), "%s", name);
			cur = -1;
		} else if (f == NULL) {
			continue;
		} else if (sscanf(line, "row %u 0x%llx", &r, &v) == 2 &&
		    r < 72) {
			f->row[r] = v;
		} else if (sscanf(line, "cluster %u bin %u n %u", &cl, &bin,
		    &n) == 3 && cl < 2 && n <= 8) {
			f->has[cl] = 1;
			f->bin[cl] = bin;
			f->n[cl] = 0;
			cur = (int)cl;
			(void)n;
		} else if (sscanf(line, "cluster %u none", &cl) == 1 &&
		    cl < 2) {
			cur = -1;
		} else if (sscanf(line, "corner %ld %ld %ld", &khz, &ol, &fl)
		    == 3 && cur >= 0 && f->n[cur] < 8) {
			k = f->n[cur]++;
			f->khz[cur][k] = (uint32_t)khz;
			f->ol[cur][k] = (int32_t)ol;
			f->fl[cur][k] = (int32_t)fl;
		}
	}
	fclose(fp);
	return nsets > 0 ? 0 : -1;
}

const struct fuse_set *
model_fuse_set(const char *name)
{
	unsigned i;

	for (i = 0; i < nsets; i++)
		if (strcmp(sets[i].name, name) == 0)
			return &sets[i];
	return NULL;
}

unsigned
model_fuse_sets(void)
{
	return nsets;
}

const struct fuse_set *
model_fuse_set_at(unsigned i)
{
	return i < nsets ? &sets[i] : NULL;
}

/*===========================================================================*
 *    a scenario's start and end                                             *
 *===========================================================================*/
static void
rail_sid(unsigned sid, unsigned base, unsigned mv, uint8_t subtype,
	int enabled)
{
	pmic[sid][base + 0x04] = 0x1c;
	pmic[sid][base + 0x05] = subtype;
	pmic[sid][base + 0x40] = (uint8_t)mv;
	pmic[sid][base + 0x41] = (uint8_t)(mv >> 8);
	pmic[sid][base + 0x45] = 0x06;
	pmic[sid][base + 0x46] = enabled ? 0x80 : 0x00;
	pmic[sid][base + 0x68] = 0x74;	/* ULS 1396 mV: what a careless */
	pmic[sid][base + 0x69] = 0x05;	/* reader would take for a set point */
}

static void
rail(unsigned base, unsigned mv, uint8_t subtype, int enabled)
{
	rail_sid(1, base, mv, subtype, enabled);
}

void
model_reset(const struct scenario *s)
{
	static const uint32_t cfg0[2] = { 0x501, 0x401 };
	static const uint32_t l0[2] = { 0x48, 0x20 };
	struct block *b;
	unsigned i, r, c;

	sc = s;
	fs = model_fuse_set(s->fuses != NULL ? s->fuses : "tablet");
	if (fs == NULL) {
		fprintf(stderr, "model: no fuse set %s\n", s->fuses);
		exit(2);
	}
	now = 0;
	writes = 0;
	violations = 0;
	violation[0] = '\0';
	model_log_reset();

	for (i = 0; i < NBLOCK; i++) {
		b = &blocks[i];
		if (b->mem == NULL && (b->mem = calloc(1, b->size)) == NULL)
			abort();
		if (b->kind == K_FORBID) {
			for (r = 0; r < b->size; r += 4)
				put32(b->mem + r, (uint32_t)(b->pa + r) ^
				    0x5a5a0000u);
			if (b->init == NULL && (b->init = malloc(b->size)) ==
			    NULL)
				abort();
			memcpy(b->init, b->mem, b->size);
		} else if (b->kind != K_SPMI)
			memset(b->mem, 0, b->size);
	}
	b = block_named("qfprom");
	for (r = 0; r < 72; r++) {
		put32(b->mem + 0x4000 + 8 * r, (uint32_t)fs->row[r]);
		put32(b->mem + 0x4000 + 8 * r + 4,
		    (uint32_t)(fs->row[r] >> 32));
	}
	b = block_named("gcc");
	put32(b->mem + 0x4805c, s->aux_off ? 0x80000002u : 0x2u);
	put32(b->mem + 0x48060, s->aux_cfg ? s->aux_cfg : 0x100);

	for (c = 0; c < 2; c++) {
		memset(&mx[c], 0, sizeof(mx[c]));
		mx[c].latched = mx[c].active = s->cfg[c] ? s->cfg[c] : cfg0[c];
		memset(&pl[c], 0, sizeof(pl[c]));
		pl[c].mode = 7;
		pl[c].l = s->l[c] ? s->l[c] : l0[c];
		pl[c].user = 0xf;
		pl[c].config = 0x200d4828;
		pl[c].phase = 6;
		pl[c].will_lock = 1;
	}

	memset(pmic, 0, sizeof(pmic));
	rail(0x1400, s->mv[0] ? s->mv[0] : 872,
	    s->s1_subtype ? s->s1_subtype : 0x0a, 1);
	rail(0x1700, s->mv[1] ? s->mv[1] : 868, 0x0a, 1);
	rail(0x1a00, s->mv[2] ? s->mv[2] : 868, 0x0a, !s->s3_disabled);
	/* VDD_MX as the tablet had it (cpu-probe-20261018.txt). */
	rail_sid(3, 0x2000, s->mx_mv ? s->mx_mv : 884, 0x0a, 1);

	for (c = 0; c < 2; c++)
		held_at_start[c] = model_held((int)c, model_khz((int)c));
}

void
model_end_check(void)
{
	unsigned i, c;
	uint32_t khz;
	int32_t uv;
	const char *why;

	for (i = 0; i < NBLOCK; i++)
		if (blocks[i].kind == K_FORBID &&
		    memcmp(blocks[i].mem, blocks[i].init, blocks[i].size) != 0)
			V("%s changed", blocks[i].name);
	for (c = 0; c < 2; c++) {
		mux_settle((int)c);
		if (mx[c].pending && !mx[c].stuck)
			V("%s: a switch left pending", clname[c]);
		if (SRC(mx[c].active) == 5 && !pll_out((int)c))
			V("%s: the cores end on an unlocked PLL", clname[c]);
		khz = model_khz((int)c);
		if (held_at_start[c] &&
		    (why = why_not_held((int)c, khz, &uv)) != NULL)
			V("%s ends at %u kHz (open loop %d uV, set point %u mV): "
			    "%s", clname[c], khz, uv, model_setpoint((int)c), why);
	}
}

uint32_t
model_khz(int cl)
{
	return rate_of(cl, mx[cl].active);
}

const char *
model_l_history(int cl)
{
	return pl[cl].hist;
}
