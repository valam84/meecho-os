/*
 * Стенд mali: модель ускорителя, памяти и окружения службы.
 *
 * Модель знает о железе всё сама, независимо от службы, и сверяет с ним
 * каждый шаг службы:
 *
 *   - регистры: служба пишет и читает их через model_wr()/model_rd()
 *     (regs-hook.h), и модель исполняет запись тогда, когда служба её
 *     делает: сброс прерывания снимает бит, LOCK запоминает область,
 *     FLUSH_PT/FLUSH_MEM без LOCK - ошибка, с ним - выбрасывают из TLB
 *     окна всё в области; UPDATE берёт TRANSTAB и MEMATTR в работу и
 *     опустошает TLB, а без таблиц - выключает окно, и стоящее в нём на
 *     отказе задание падает (JOB_BUS_FAULT), снова поднимая бит отказа:
 *     так на плате (port/cb2-gpu/ref-fault.txt, meecho-fault.txt);
 *   - отказ: задание стоит на нём, пока FLUSH_PT, FLUSH_MEM или UNLOCK -
 *     любого окна - не повторит доступ (kbase: «flush L2 and unlock the
 *     VA (resumes the MMU)», и что FLUSH повторяет стоящие доступы и в
 *     других окнах).  Повтор - сразу, внутри команды: так же быстро, как
 *     на железе, где ускоритель идёт дальше, пока служба ещё в
 *     обработчике.  Бит, поднятый и не сброшенный, держит адрес и
 *     состояние первого отказа: новый отказ под ним не виден (kbase:
 *     «a new IRQ might not be raised when the GPU finds a MMU IRQ is
 *     already pending»);
 *   - TLB: у каждого окна свой; перевод, раз найденный, берётся из него,
 *     пока его не выбросят.  Служба, забывшая сброс после того, как сняла
 *     буфер с таблиц, получит ускоритель, пишущий в страницу, которую она
 *     уже вернула, - и модель скажет «not live»;
 *   - память: у службы виртуальные страницы, у ОЗУ - физические, и
 *     страницы mmap() разбросаны по ОЗУ (не подряд), а alloc_contig() -
 *     подряд.  Кэш процессора некогерентен: вид процессора, вид ОЗУ и
 *     «тень» - вид процессора на момент последнего обслуживания строки;
 *     строка грязная, если процессор менял её с тех пор.  Ускоритель,
 *     читающий или пишущий грязную строку, - ошибка; процессор, не
 *     сбросивший строку после ускорителя, читает старое.  Память от VM -
 *     нули, записанные процессором поверх того, что оставил прежний
 *     хозяин в ОЗУ: без чистки ускоритель увидел бы чужое.  Чистка
 *     пишет в ОЗУ только грязную строку (целиком), сброс без чистки
 *     грязную строку теряет - как DC CVAC/CIVAC и DC IVAC;
 *   - проход по таблицам - по правилам io-pgtable ARM_MALI_LPAE: четыре
 *     уровня всегда, таблица - тип 3, лист - тип 1 на любом уровне, права
 *     на чтение и запись (биты 7:6), разделяемость «внешняя» (9:8 = 2);
 *   - дескриптор - по раскладке, которой зонд port/cb2-gpu/jobprobe.py
 *     прогнал на плате цепочку из 64 заданий через panfrost.
 *
 * Окружение: гранты VFS (размер - из номера запроса), память процесса,
 * от которого идёт вызов (sys_vircopy() к другому процессу - нарушение),
 * vm_remap(), отложенные ответы, таймеры libsys по часам модели.
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>

#include <minix/cachectl.h>
#include <minix/ds.h>

#include "model.h"

struct mali mali;
struct log mali_log = { "mali", LEVEL_INFO, NULL };

unsigned checks, failures;
int verbose;

/* ---- журнал ------------------------------------------------------------ */

char logbuf[262144];
static size_t loglen;
unsigned warnings;

void
shim_log(struct log *l, int level, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (level > l->log_level)
		return;
	if (level == LEVEL_WARN)
		warnings++;
	va_start(ap, fmt);
	n = vsnprintf(logbuf + loglen, sizeof(logbuf) - loglen, fmt, ap);
	va_end(ap);
	if (verbose)
		printf("    | %s", logbuf + loglen);
	if (n > 0 && loglen + (size_t)n < sizeof(logbuf))
		loglen += (size_t)n;
}

int
said(const char *what)
{
	return strstr(logbuf, what) != NULL;
}

void
log_clear(void)
{
	loglen = 0;
	logbuf[0] = '\0';
	warnings = 0;
}

/* ---- ошибки модели ------------------------------------------------------ */

unsigned model_errors;
char model_error_text[512];

static void
model_error(const char *fmt, ...)
{
	va_list ap;

	model_errors++;
	va_start(ap, fmt);
	vsnprintf(model_error_text, sizeof(model_error_text), fmt, ap);
	va_end(ap);
	if (verbose)
		printf("    ! %s\n", model_error_text);
}

/* ---- память ------------------------------------------------------------ */

static unsigned char *cpu_view, *shadow, *ram_view;
static int v2p[ARENA_PAGES];
static unsigned char live[ARENA_PAGES];		/* по физической странице */
static unsigned vnext, scatter_next, contig_next;

static struct alloc {
	unsigned v0, n;
	int contig, freed;
} allocs[4096];
static unsigned nallocs;

#define SCATTER_PAGES	(ARENA_PAGES / 2)

static void *
arena_alloc(size_t len, int contig)
{
	unsigned n = (unsigned)((len + PAGE - 1) / PAGE), i, p;
	struct alloc *a;

	if (n == 0 || vnext + n > ARENA_PAGES || nallocs == 4096)
		return NULL;
	if (contig && contig_next + n > ARENA_PAGES)
		return NULL;
	if (!contig && scatter_next + n > SCATTER_PAGES)
		return NULL;
	a = &allocs[nallocs++];
	a->v0 = vnext;
	a->n = n;
	a->contig = contig;
	a->freed = 0;
	for (i = 0; i < n; i++) {
		/*
		 * mmap(): k-я страница - на (k * 3533) mod SCATTER_PAGES, то
		 * есть соседние виртуальные страницы далеко друг от друга в
		 * ОЗУ.  alloc_contig(): подряд, из верхней половины.
		 */
		if (contig)
			p = contig_next++;
		else
			p = (scatter_next++ * 3533u) % SCATTER_PAGES;
		v2p[vnext + i] = (int)p;
		live[p] = 1;
		/*
		 * VM даёт нули - записанные процессором через кэш; в ОЗУ
		 * лежит то, что оставил прежний хозяин страницы.
		 */
		memset(ram_view + (size_t)p * PAGE, 0x5e, PAGE);
		memset(shadow + (size_t)(vnext + i) * PAGE, 0x5e, PAGE);
		memset(cpu_view + (size_t)(vnext + i) * PAGE, 0, PAGE);
	}
	vnext += n;
	return cpu_view + (size_t)a->v0 * PAGE;
}

static struct alloc *
alloc_of(const void *addr)
{
	unsigned i, v;

	if ((const unsigned char *)addr < cpu_view ||
	    (const unsigned char *)addr >= cpu_view + (size_t)ARENA_PAGES * PAGE)
		return NULL;
	v = (unsigned)(((const unsigned char *)addr - cpu_view) / PAGE);
	for (i = 0; i < nallocs; i++)
		if (!allocs[i].freed && v >= allocs[i].v0 &&
		    v < allocs[i].v0 + allocs[i].n)
			return &allocs[i];
	return NULL;
}

static int
arena_free(void *addr, size_t len, int contig)
{
	struct alloc *a = alloc_of(addr);
	unsigned i, p;

	if (a == NULL || cpu_view + (size_t)a->v0 * PAGE != addr ||
	    (len + PAGE - 1) / PAGE != a->n || a->contig != contig) {
		model_error("free of %p, %zu bytes: not an allocation", addr,
		    len);
		return -1;
	}
	for (i = 0; i < a->n; i++) {
		p = (unsigned)v2p[a->v0 + i];
		live[p] = 0;
		/* Следующий хозяин страницы увидит это. */
		memset(ram_view + (size_t)p * PAGE, 0xfe, PAGE);
	}
	a->freed = 1;
	return 0;
}

void *
alloc_contig(size_t len, int flags, phys_bytes *phys)
{
	void *v;

	(void)flags;
	if ((v = arena_alloc(len, 1)) == NULL)
		return NULL;
	if (phys != NULL)
		*phys = (phys_bytes)model_pa(v);
	return v;
}

int
free_contig(void *addr, size_t len)
{
	return arena_free(addr, len, 1);
}

void *
model_mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	void *v;

	(void)addr;
	(void)prot;
	(void)off;
	if (!(flags & MAP_ANON) || !(flags & MAP_PREALLOC) || fd != -1) {
		model_error("mmap without MAP_ANON|MAP_PREALLOC");
		return MAP_FAILED;
	}
	if ((v = arena_alloc(len, 0)) == NULL)
		return MAP_FAILED;
	return v;
}

int
model_munmap(void *addr, size_t len)
{
	return arena_free(addr, len, 0);
}

void *
model_last_mmap(size_t *len)
{
	int i;

	for (i = (int)nallocs - 1; i >= 0; i--)
		if (!allocs[i].contig && !allocs[i].freed) {
			*len = (size_t)allocs[i].n * PAGE;
			return cpu_view + (size_t)allocs[i].v0 * PAGE;
		}
	*len = 0;
	return NULL;
}

unsigned
model_live_pages(void)
{
	unsigned p, n = 0;

	for (p = 0; p < ARENA_PAGES; p++)
		n += live[p];
	return n;
}

unsigned
model_allocs_live(void)
{
	unsigned i, n = 0;

	for (i = 0; i < nallocs; i++)
		n += !allocs[i].freed;
	return n;
}

uint64_t
model_pa(const void *cpu)
{
	size_t off = (const unsigned char *)cpu - cpu_view;

	return PHYS_BASE + (uint64_t)v2p[off / PAGE] * PAGE + off % PAGE;
}

unsigned char *
model_ram(uint64_t pa, size_t len)
{
	uint64_t off;

	if (pa < PHYS_BASE || pa >= PHYS_BASE + (uint64_t)ARENA_PAGES * PAGE)
		return NULL;
	off = pa - PHYS_BASE;
	if (off % PAGE + len > PAGE)
		return NULL;
	return ram_view + off;
}

int
model_page_live(uint64_t pa)
{
	if (pa < PHYS_BASE || pa >= PHYS_BASE + (uint64_t)ARENA_PAGES * PAGE)
		return 0;
	return live[(pa - PHYS_BASE) / PAGE];
}

/* Строка процессора для физического адреса: её вид и тень; NULL - нет. */
static int
cpu_line_of(uint64_t pa, unsigned char **cv, unsigned char **sh)
{
	unsigned p = (unsigned)((pa - PHYS_BASE) / PAGE), v;

	for (v = 0; v < vnext; v++)
		if ((unsigned)v2p[v] == p) {
			size_t off = (size_t)v * PAGE + (pa % PAGE &
			    ~(uint64_t)(LINE - 1));
			*cv = cpu_view + off;
			*sh = shadow + off;
			return 1;
		}
	return 0;
}

/* Грязна ли строка физического адреса в кэше процессора. */
static int
line_dirty(uint64_t pa)
{
	unsigned char *cv, *sh;

	if (!cpu_line_of(pa, &cv, &sh))
		return 0;
	return memcmp(cv, sh, LINE) != 0;
}

int
sys_umap(endpoint_t ep, int seg, vir_bytes addr, vir_bytes len,
	phys_bytes *phys)
{
	size_t off = (unsigned char *)addr - cpu_view, k;

	if (ep != SELF || seg != VM_D || alloc_of((void *)addr) == NULL)
		return EFAULT;
	/* Как ядро: только физически непрерывный диапазон. */
	for (k = PAGE; k < len + off % PAGE; k += PAGE)
		if (v2p[(off + k) / PAGE] != v2p[off / PAGE] + (int)(k / PAGE))
			return EFAULT;
	*phys = (phys_bytes)model_pa((void *)addr);
	return OK;
}

/* ---- кэш --------------------------------------------------------------- */

static struct { int op; size_t off, len; } cops[16384];
unsigned ncops;

int
sys_cachectl(int op, void *addr, size_t len)
{
	size_t off = (unsigned char *)addr - cpu_view, lo, hi, l;
	unsigned char *cv, *sh, *rm;

	if ((unsigned char *)addr < cpu_view ||
	    off + len > (size_t)ARENA_PAGES * PAGE)
		return EFAULT;
	if (ncops < sizeof(cops) / sizeof(cops[0])) {
		cops[ncops].op = op;
		cops[ncops].off = off;
		cops[ncops].len = len;
		ncops++;
	}
	/* Целыми строками, как ядро; неполная строка сначала пишется. */
	lo = off & ~(size_t)(LINE - 1);
	hi = (off + len + LINE - 1) & ~(size_t)(LINE - 1);
	for (l = lo; l < hi; l += LINE) {
		cv = cpu_view + l;
		sh = shadow + l;
		rm = ram_view + (size_t)v2p[l / PAGE] * PAGE + l % PAGE;
		if ((op == CACHE_CLEAN || op == CACHE_CLEAN_INVALIDATE ||
		    l < off || l + LINE > off + len) &&
		    memcmp(cv, sh, LINE) != 0) {
			/* Только грязная: чистая строка в ОЗУ не пишется. */
			memcpy(rm, cv, LINE);
			memcpy(sh, cv, LINE);
		}
		if (op == CACHE_INVALIDATE || op == CACHE_CLEAN_INVALIDATE) {
			memcpy(cv, rm, LINE);
			memcpy(sh, rm, LINE);
		}
	}
	return 0;
}

/* Покрыт ли [addr, addr+len) операциями op (или CLEAN_INVALIDATE) подряд. */
int
covered(int op, const void *addr, size_t len, unsigned from)
{
	size_t off = (const unsigned char *)addr - cpu_view, at = off;
	unsigned i;
	int moved = 1;

	while (moved && at < off + len) {
		moved = 0;
		for (i = from; i < ncops; i++)
			if ((cops[i].op == op ||
			    cops[i].op == CACHE_CLEAN_INVALIDATE) &&
			    cops[i].off <= at && cops[i].off + cops[i].len > at) {
				at = cops[i].off + cops[i].len;
				moved = 1;
			}
	}
	return at >= off + len;
}

/* ---- регистры и MMU ------------------------------------------------------ */

uint32_t model_regs[0x4000 / 4];

#define TLB_ENT		128
static struct tlbe {
	int valid;
	uint64_t va, pte;
} tlb[MALI_NR_AS][TLB_ENT];
static unsigned tlb_next[MALI_NR_AS];
static uint64_t active_tt[MALI_NR_AS];	/* TRANSTAB после UPDATE */
static int locked[MALI_NR_AS];
static uint64_t lock_start[MALI_NR_AS], lock_size[MALI_NR_AS];

static uint32_t cmd_hist[MALI_NR_AS][64];
static unsigned ncmd[MALI_NR_AS];

static struct { int as; uint64_t start, size; } flushes[4096];
static unsigned nflushes;

static struct {
	int busy;		/* задание дано и не кончилось */
	int pending;		/* START записан, модель его не исполнила */
	int stalled, stall_as;
	uint64_t stall_jc;	/* дескриптор, на котором стоит */
	unsigned done;		/* исполнено с START */
	int hang, ignore_stop;
	int fail;		/* упасть с fail_status, не исполняясь */
	uint32_t fail_status;
} sl[MALI_NR_SLOTS];

static int fail_at_once;

static void retry_stalled(void);

static void
tlb_drop(int as, uint64_t start, uint64_t size)
{
	unsigned i;

	for (i = 0; i < TLB_ENT; i++)
		if (tlb[as][i].valid && (size == 0 ||
		    (tlb[as][i].va >= start && tlb[as][i].va - start < size)))
			tlb[as][i].valid = 0;
}

static void
job_fail(int s, uint32_t status)
{
	G(JOB_INT_RAWSTAT) |= JOB_INT_FAILED(s);
	G(JS_STATUS(s)) = status;
	sl[s].busy = sl[s].pending = sl[s].stalled = 0;
}

static void
as_command(int as, uint32_t cmd)
{
	uint64_t v;
	unsigned w, s;

	if (ncmd[as] < 64)
		cmd_hist[as][ncmd[as]++] = cmd;
	switch (cmd) {
	case AS_COMMAND_LOCK:
		v = G(AS_LOCKADDR_LO(as)) | ((uint64_t)G(AS_LOCKADDR_HI(as)) <<
		    32);
		w = (unsigned)(v & 0x3f);
		locked[as] = 1;
		if (w >= 63) {
			lock_start[as] = 0;
			lock_size[as] = 0;	/* всё */
		} else {
			lock_size[as] = 1ull << (w + 1);
			lock_start[as] = (v & ~0x3full) & ~(lock_size[as] - 1);
		}
		break;
	case AS_COMMAND_UNLOCK:
		locked[as] = 0;
		retry_stalled();
		break;
	case AS_COMMAND_FLUSH_PT:
	case AS_COMMAND_FLUSH_MEM:
		if (!locked[as]) {
			model_error("address space %d: flush without a lock", as);
			tlb_drop(as, 0, 0);
			break;
		}
		tlb_drop(as, lock_start[as], lock_size[as]);
		if (nflushes < 4096) {
			flushes[nflushes].as = as;
			flushes[nflushes].start = lock_start[as];
			flushes[nflushes].size = lock_size[as];
			nflushes++;
		}
		locked[as] = 0;
		retry_stalled();
		break;
	case AS_COMMAND_UPDATE:
		active_tt[as] = G(AS_TRANSTAB_LO(as)) |
		    ((uint64_t)G(AS_TRANSTAB_HI(as)) << 32);
		tlb_drop(as, 0, 0);
		if ((active_tt[as] & 3) != 3) {
			/* Окно выключено: стоящее на отказе задание падает. */
			for (s = 0; s < MALI_NR_SLOTS; s++)
				if (sl[s].stalled && sl[s].stall_as == (int)as) {
					job_fail((int)s, 0x48);
					G(MMU_INT_RAWSTAT) |=
					    MMU_INT_PAGE_FAULT(as);
				}
		} else if (G(AS_MEMATTR_LO(as)) != 0x888d88)
			model_error("address space %d on with MEMATTR 0x%x",
			    as, G(AS_MEMATTR_LO(as)));
		break;
	default:
		model_error("address space %d: command %u", as, cmd);
	}
}

uint32_t
model_rd(const struct mali_regs *r, unsigned off)
{
	(void)r;
	switch (off) {
	case JOB_INT_STAT:
		return G(JOB_INT_RAWSTAT) & G(JOB_INT_MASK);
	case MMU_INT_STAT:
		return G(MMU_INT_RAWSTAT) & G(MMU_INT_MASK);
	case GPU_INT_STAT:
		return G(GPU_INT_RAWSTAT) & G(GPU_INT_MASK);
	case GPU_LATEST_FLUSH_ID:
		return 0x42;
	}
	if (off >= AS_BASE(0) && off < AS_BASE(MALI_NR_AS) &&
	    (off - AS_BASE(0)) % 0x40 == 0x28)
		return 0;		/* AS_STATUS: никогда не занято */
	return G(off);
}

void
model_wr(const struct mali_regs *r, unsigned off, uint32_t v)
{
	int s;

	(void)r;
	if (off >= sizeof(model_regs)) {
		model_error("write past the registers: 0x%x", off);
		return;
	}
	switch (off) {
	case JOB_INT_CLEAR:
		G(JOB_INT_RAWSTAT) &= ~v;
		return;
	case MMU_INT_CLEAR:
		G(MMU_INT_RAWSTAT) &= ~v;
		return;
	case GPU_INT_CLEAR:
		G(GPU_INT_RAWSTAT) &= ~v;
		return;
	}
	if (off >= AS_BASE(0) && off < AS_BASE(MALI_NR_AS) &&
	    (off - AS_BASE(0)) % 0x40 == 0x18) {
		G(off) = v;
		as_command((int)((off - AS_BASE(0)) / 0x40), v);
		return;
	}
	for (s = 0; s < MALI_NR_SLOTS; s++) {
		if (off == (unsigned)JS_COMMAND_NEXT(s) &&
		    v == JS_COMMAND_START) {
			if (sl[s].busy)
				model_error("slot %d started while busy", s);
			G(JS_HEAD_LO(s)) = G(JS_HEAD_NEXT_LO(s));
			G(JS_HEAD_HI(s)) = G(JS_HEAD_NEXT_HI(s));
			G(JS_CONFIG(s)) = G(JS_CONFIG_NEXT(s));
			G(off) = 0;	/* «следующее» стало текущим */
			sl[s].busy = sl[s].pending = 1;
			sl[s].done = 0;
			return;
		}
		if (off == (unsigned)JS_COMMAND(s) &&
		    v == JS_COMMAND_HARD_STOP) {
			G(off) = v;
			if (sl[s].busy && !sl[s].ignore_stop)
				job_fail(s, JS_STATUS_TERMINATED);
			return;
		}
	}
	G(off) = v;
}

static int
walk(uint64_t transtab, uint64_t va, uint64_t *pte, uint32_t *exc)
{
	uint64_t table, e;
	unsigned char *p;
	int lvl;

	*exc = 0;
	if ((transtab & 3) != 3) {
		snprintf(why, sizeof(why), "TRANSTAB adrmode %u, not table",
		    (unsigned)(transtab & 3));
		*exc = 0xc0;
		return -1;
	}
	table = transtab & 0x000ffffffffff000ull;
	for (lvl = 0; lvl < 4; lvl++) {
		unsigned idx = (unsigned)(va >> (39 - 9 * lvl)) & 0x1ff;

		if (!model_page_live(table) ||
		    (p = model_ram(table + 8 * idx, 8)) == NULL) {
			snprintf(why, sizeof(why), "level %d table at 0x%llx is "
			    "not live memory of the service", lvl,
			    (unsigned long long)table);
			model_error("%s", why);
			return -1;
		}
		if (line_dirty(table + 8 * idx)) {
			snprintf(why, sizeof(why), "level %d entry for 0x%llx "
			    "read while dirty in the processor's cache", lvl,
			    (unsigned long long)va);
			model_error("%s", why);
			return -1;
		}
		memcpy(&e, p, 8);
		if (lvl < 3) {
			if ((e & 3) != 3) {
				snprintf(why, sizeof(why), "level %d entry %u "
				    "for 0x%llx: 0x%llx is not a table", lvl, idx,
				    (unsigned long long)va,
				    (unsigned long long)e);
				*exc = 0xc0u + (uint32_t)lvl;
				return -1;
			}
			table = e & 0x000ffffffffff000ull;
			continue;
		}
		if ((e & 3) != 1) {
			snprintf(why, sizeof(why), "level 3 entry for 0x%llx: "
			    "type %u (a Mali LPAE leaf is 1; 3 is the CPU's)",
			    (unsigned long long)va, (unsigned)(e & 3));
			*exc = 0xc3;
			return -1;
		}
		if (((e >> 8) & 3) != 2) {
			snprintf(why, sizeof(why), "0x%llx: shareability %u, "
			    "panfrost's is outer (2)", (unsigned long long)va,
			    (unsigned)((e >> 8) & 3));
			model_error("%s", why);
			return -1;
		}
		*pte = e;
		return 0;
	}
	return -1;
}

/* Перевод: из TLB окна или проходом по активным таблицам. */
static int
xlate(int as, uint64_t va, int write, uint64_t *pa, uint32_t *exc)
{
	uint64_t page = va & ~(uint64_t)(PAGE - 1), pte = 0;
	unsigned i;
	int hit = 0;

	for (i = 0; i < TLB_ENT; i++)
		if (tlb[as][i].valid && tlb[as][i].va == page) {
			pte = tlb[as][i].pte;
			hit = 1;
			break;
		}
	if (!hit) {
		if (walk(active_tt[as], va, &pte, exc) != 0)
			return -1;
		tlb[as][tlb_next[as]].valid = 1;
		tlb[as][tlb_next[as]].va = page;
		tlb[as][tlb_next[as]].pte = pte;
		tlb_next[as] = (tlb_next[as] + 1) % TLB_ENT;
	}
	if (((pte >> 6) & 1) == 0 || (write && ((pte >> 7) & 1) == 0)) {
		snprintf(why, sizeof(why), "0x%llx: %s not permitted (entry "
		    "0x%llx)", (unsigned long long)va, write ? "write" : "read",
		    (unsigned long long)pte);
		*exc = 0xcb;
		return -1;
	}
	*pa = (pte & 0x000ffffffffff000ull) | (va & (PAGE - 1));
	if (!model_page_live(*pa)) {
		snprintf(why, sizeof(why), "the GPU reached 0x%llx through "
		    "0x%llx, a page that is not live: the service gave it back "
		    "while the GPU could still reach it", (unsigned long long)*pa,
		    (unsigned long long)va);
		model_error("%s", why);
		*exc = 0;
		return -1;
	}
	return 0;
}

/*
 * Отказ MMU: бит окна, состояние, адрес; задание стоит на дескрипторе jc
 * (или падает).  Под поднятым битом состояние и адрес - первого отказа.
 */
static void
mmu_fault(int s, int as, uint64_t jc, uint64_t va, int write, uint32_t exc)
{
	if (!(G(MMU_INT_RAWSTAT) & MMU_INT_AS(as))) {
		G(AS_FAULTSTATUS(as)) = exc |
		    (uint32_t)(write ? AS_ACCESS_WRITE : AS_ACCESS_READ) << 8 |
		    0x7c10u << 16;
		G(AS_FAULTADDRESS_LO(as)) = (uint32_t)va;
		G(AS_FAULTADDRESS_HI(as)) = (uint32_t)(va >> 32);
	}
	G(MMU_INT_RAWSTAT) |= MMU_INT_PAGE_FAULT(as);
	if (fail_at_once)
		job_fail(s, 0x48);
	else {
		sl[s].stalled = 1;
		sl[s].stall_as = as;
		sl[s].stall_jc = jc;
		sl[s].pending = 0;
	}
}

static uint64_t
rd64(const unsigned char *p, unsigned off)
{
	uint64_t v;

	memcpy(&v, p + off, 8);
	return v;
}

static uint32_t
rd32(const unsigned char *p, unsigned off)
{
	uint32_t v;

	memcpy(&v, p + off, 4);
	return v;
}

char why[256];

/* Цепочка слота s в окне as с дескриптора va: сколько исполнено, -1. */
static int
run_chain(int s, int as, uint64_t va)
{
	uint64_t pa, dst;
	unsigned char *d, *t;
	unsigned seen[1024] = { 0 }, idx;
	uint32_t exc;
	int n = 0;

	while (va != 0) {
		if (n >= 1024) {
			snprintf(why, sizeof(why), "the chain does not end");
			return -1;
		}
		if (xlate(as, va, 0, &pa, &exc) != 0) {
			if (exc == 0)
				return -1;
			mmu_fault(s, as, va, va, 0, exc);
			return n;
		}
		if ((d = model_ram(pa, JOB_DESC_SIZE)) == NULL) {
			snprintf(why, sizeof(why), "descriptor across a page");
			return -1;
		}
		if (line_dirty(pa)) {
			snprintf(why, sizeof(why), "job %d: its descriptor is "
			    "dirty in the processor's cache", n);
			model_error("%s", why);
			return -1;
		}
		if ((rd32(d, 16) & 1) != 1) {
			snprintf(why, sizeof(why), "job %d: not 64-bit", n);
			return -1;
		}
		if (((rd32(d, 16) >> 1) & 0x7f) != 2) {
			snprintf(why, sizeof(why), "job %d: type %u, not "
			    "WRITE_VALUE", n, (rd32(d, 16) >> 1) & 0x7f);
			return -1;
		}
		idx = rd32(d, 16) >> 16;
		if (idx == 0 || idx >= 1024 || seen[idx]) {
			snprintf(why, sizeof(why), "job %d: index %u zero or "
			    "repeated", n, idx);
			return -1;
		}
		seen[idx] = 1;
		if (rd32(d, 40) != 6) {
			snprintf(why, sizeof(why), "job %d: value type %u, not "
			    "32-bit immediate", n, rd32(d, 40));
			return -1;
		}
		if (xlate(as, rd64(d, 32), 1, &dst, &exc) != 0) {
			if (exc == 0)
				return -1;
			mmu_fault(s, as, va, rd64(d, 32), 1, exc);
			return n;
		}
		if ((t = model_ram(dst, 4)) == NULL)
			return -1;
		if (line_dirty(dst)) {
			snprintf(why, sizeof(why), "job %d writes 0x%llx, whose "
			    "line is dirty in the processor's cache", n,
			    (unsigned long long)dst);
			model_error("%s", why);
			return -1;
		}
		memcpy(t, d + 48, 4);
		memset(d, 0, 4);
		d[0] = JOB_STATUS_DONE;
		va = rd64(d, 24);
		n++;
		sl[s].done++;
	}
	G(JOB_INT_RAWSTAT) |= JOB_INT_DONE(s);
	G(JS_STATUS(s)) = JS_STATUS_DONE;
	sl[s].busy = 0;
	return n;
}

int
model_run(int s)
{
	int as;

	if (!sl[s].busy || !sl[s].pending) {
		snprintf(why, sizeof(why), "nothing started on slot %d", s);
		return -1;
	}
	if (sl[s].hang)
		return 0;
	sl[s].pending = 0;
	if (sl[s].fail) {
		sl[s].fail = 0;
		job_fail(s, sl[s].fail_status);
		return 0;
	}
	as = (int)(G(JS_CONFIG(s)) & 0xf);
	if ((active_tt[as] & 3) != 3) {
		snprintf(why, sizeof(why), "slot %d started in address space "
		    "%d, which has no tables", s, as);
		model_error("%s", why);
		return -1;
	}
	if ((G(JS_CONFIG(s)) & 0xff00) != 0x7300 &&
	    (G(JS_CONFIG(s)) & 0xff00) != 0x7b00)
		model_error("slot %d config 0x%x", s, G(JS_CONFIG(s)));
	return run_chain(s, as, G(JS_HEAD_LO(s)) |
	    ((uint64_t)G(JS_HEAD_HI(s)) << 32));
}

/*
 * Команда окна, которая повторяет стоящие доступы: каждое задание, стоящее
 * на отказе в окне с таблицами, пробует снова с того же дескриптора -
 * сразу.  Не нашло перевода опять - снова стоит.
 */
static void
retry_stalled(void)
{
	int s;

	for (s = 0; s < MALI_NR_SLOTS; s++) {
		if (!sl[s].stalled || (active_tt[sl[s].stall_as] & 3) != 3)
			continue;
		sl[s].stalled = 0;
		if (run_chain(s, sl[s].stall_as, sl[s].stall_jc) < 0)
			model_error("slot %d after the retry: %s", s, why);
	}
}

unsigned
model_slot_done(int s)
{
	return sl[s].done;
}

int
model_slot_pending(int s)
{
	return sl[s].busy && sl[s].pending;
}

void
model_hang(int s, int ignore_stop)
{
	sl[s].hang = 1;
	sl[s].ignore_stop = ignore_stop;
}

void
model_fail(int s, uint32_t js_status)
{
	sl[s].fail = 1;
	sl[s].fail_status = js_status;
}

void
model_fail_at_once(int on)
{
	fail_at_once = on;
}

uint32_t
model_as_cmds(int as, uint32_t *last, unsigned n)
{
	unsigned i, k = ncmd[as] < n ? ncmd[as] : n;

	for (i = 0; i < k; i++)
		last[i] = cmd_hist[as][ncmd[as] - k + i];
	return ncmd[as] ? cmd_hist[as][ncmd[as] - 1] : 0;
}

uint64_t
model_as_active(int as)
{
	return active_tt[as];
}

unsigned
model_flush_mark(void)
{
	return nflushes;
}

/* Сброшен ли из окна as диапазон [va, va+len) после отметки from. */
int
model_flushed(int as, uint64_t va, uint64_t len, unsigned from)
{
	unsigned i;

	for (i = from; i < nflushes; i++)
		if (flushes[i].as == as && (flushes[i].size == 0 ||
		    (va >= flushes[i].start && va + len <= flushes[i].start +
		    flushes[i].size)))
			return 1;
	return 0;
}

uint64_t *
model_pte(int as, uint64_t va)
{
	uint64_t table = active_tt[as] & 0x000ffffffffff000ull, e = 0;
	unsigned char *cv, *sh;
	int lvl;

	for (lvl = 0; lvl < 4; lvl++) {
		uint64_t at = table + 8 * ((va >> (39 - 9 * lvl)) & 0x1ff);

		if (!cpu_line_of(at, &cv, &sh))
			return NULL;
		if (lvl == 3)
			return (uint64_t *)(cv + (at % LINE));
		memcpy(&e, cv + (at % LINE), 8);
		table = e & 0x000ffffffffff000ull;
	}
	return NULL;
}

/*
 * Отказ, которого задания не делали: бит (страницы или шины), состояние и
 * адрес - как их оставило бы железо.  Никто на нём не стоит.
 */
void
model_mmu_raise(int as, uint32_t bits, uint32_t status, uint64_t va)
{
	G(AS_FAULTSTATUS(as)) = status;
	G(AS_FAULTADDRESS_LO(as)) = (uint32_t)va;
	G(AS_FAULTADDRESS_HI(as)) = (uint32_t)(va >> 32);
	G(MMU_INT_RAWSTAT) |= bits;
}

/* ---- линии, как irq_handle() в mali.c ------------------------------------ */

unsigned lines_job, lines_mmu;

int
model_line(int line)
{
	uint32_t s;

	if (line == MALI_IRQ_JOB) {
		if ((s = G(JOB_INT_RAWSTAT) & G(JOB_INT_MASK)) == 0)
			return 0;
		lines_job++;
		model_wr(NULL, JOB_INT_CLEAR, s);
		mali_sched_job_irq(s);
		return 1;
	}
	/* Линию MMU служба сбрасывает сама, внутри обработчика. */
	if ((s = G(MMU_INT_RAWSTAT) & G(MMU_INT_MASK)) == 0)
		return 0;
	lines_mmu++;
	mali_mmu_irq(s);
	return 1;
}

/* ---- часы, таймеры -------------------------------------------------------- */

static clock_t now_ticks;
static minix_timer_t *timers;
static u64_t frclock;

clock_t
model_now(void)
{
	return now_ticks;
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
	tp->tmr_exp_time = now_ticks + ticks;
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
model_timer_pending(void)
{
	return timers != NULL;
}

void
model_advance(clock_t ticks)
{
	now_ticks += ticks;
	frclock += (u64_t)ticks * 10000;
	expire_timers(now_ticks);
}

int
getuptime(clock_t *ticks, clock_t *realtime, time_t *boottime)
{
	*ticks = now_ticks;
	*realtime = now_ticks;
	*boottime = 1000;
	return OK;
}

void read_frclock_64(u64_t *f) { *f = frclock += 7; }
u64_t delta_frclock_64(u64_t b, u64_t c) { return c - b; }
u32_t frclock_64_to_micros(u64_t t) { return (u32_t)t; }
u32_t sys_hz(void) { return 100; }
int sys_setalarm(long t, int abs) { (void)t; (void)abs; return 0; }
int micro_delay(u32_t usec) { (void)usec; return 0; }
void *vm_map_phys(endpoint_t w, void *p, size_t l) { (void)w; (void)p;
	(void)l; return NULL; }

/* ---- VFS, VM, процессы ---------------------------------------------------- */

endpoint_t model_user;
unsigned vircopy_foreign;

static struct { void *buf; size_t len; } grants[64];
static unsigned ngrants;

cp_grant_id_t
model_grant(void *buf, size_t len)
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

	if (ep != VFS_EP || g < 1000 || i >= 64 || off + len > grants[i].len)
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
	if (other != model_user) {
		vircopy_foreign++;
		model_error("sys_vircopy with %d, not the caller %d", other,
		    model_user);
		return EPERM;
	}
	if ((sp != SELF && sv < 4096) || (dp != SELF && dv < 4096))
		return EFAULT;
	memcpy((void *)dv, (void *)sv, n);
	return OK;
}

static struct { cdev_id_t id; int r; } replies[256];
static unsigned nreplies;

void
chardriver_reply_task(endpoint_t endpt, cdev_id_t id, int r)
{
	if (endpt != VFS_EP)
		model_error("a reply to %d, not VFS", endpt);
	if (nreplies < 256) {
		replies[nreplies].id = id;
		replies[nreplies].r = r;
		nreplies++;
	}
}

int
model_reply(cdev_id_t id, int *r)
{
	unsigned i;

	for (i = 0; i < nreplies; i++)
		if (replies[i].id == id) {
			*r = replies[i].r;
			return 1;
		}
	return 0;
}

unsigned
model_replies(void)
{
	return nreplies;
}

struct model_remap remaps[64];
unsigned nremaps;

/* Endpoint службы, как его даёт sef_self(). */
endpoint_t
sef_self(void)
{
	return MODEL_SELF_EP;
}

static void *
remap(endpoint_t d, endpoint_t s, void *da, void *sa, size_t size, int ro)
{
	struct alloc *a = alloc_of(sa);

	/*
	 * VM: оба конца - настоящие endpoint'ы (vm_isokendpt(); SELF им не
	 * является - на плате так и отказало, молча), источник - начало
	 * области, и её размер целиком.
	 */
	if (s != MODEL_SELF_EP || a == NULL || a->contig ||
	    cpu_view + (size_t)a->v0 * PAGE != sa ||
	    (size_t)a->n * PAGE != size)
		return MAP_FAILED;
	if (nremaps < 64) {
		remaps[nremaps].who = d;
		remaps[nremaps].src = sa;
		remaps[nremaps].addr = da;
		remaps[nremaps].len = size;
		remaps[nremaps].ro = ro;
		nremaps++;
	}
	return sa;	/* процесс видит те же страницы, что и служба */
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

/* mali.c: сброс зависшего ускорителя - здесь без железа. */
unsigned model_recovers;

int
mali_gpu_recover(void)
{
	int s;

	model_recovers++;
	mali_sched_fail_all(EIO);
	mali_mmu_reset();
	for (s = 0; s < MALI_NR_SLOTS; s++)
		memset(&sl[s], 0, sizeof(sl[s]));
	memset(active_tt, 0, sizeof(active_tt));
	memset(tlb, 0, sizeof(tlb));
	G(JOB_INT_RAWSTAT) = 0;
	G(MMU_INT_RAWSTAT) = 0;
	G(MMU_INT_MASK) = 0;
	mali_sched_init();
	mali_sched_kick();
	return OK;
}

/* ---- сброс модели ---------------------------------------------------------- */

/* ---- DS и чужая память: ввоз буферов развёртки fb (meecho_drm.h) ------- */

/*
 * Ключи, которые fb публикует в DS; у модели их ставит стенд.  Метки fb в
 * модели нет: служба остаётся без своего вида буфера (его нельзя
 * отобразить клиенту), а ускорителю он отдаётся всё равно.
 */
static struct { char key[DS_MAX_KEYLEN]; u32_t val; } dskeys[16];
static unsigned ndskeys;

void
model_ds_set(const char *key, u32_t val)
{
	unsigned i;

	for (i = 0; i < ndskeys && strcmp(dskeys[i].key, key) != 0; i++)
		;
	if (i == ndskeys) {
		if (ndskeys == sizeof(dskeys) / sizeof(dskeys[0])) {
			model_error("DS: too many keys");
			return;
		}
		snprintf(dskeys[ndskeys++].key, DS_MAX_KEYLEN, "%s", key);
	}
	dskeys[i].val = val;
}

int
ds_retrieve_u32(const char *key, u32_t *val)
{
	unsigned i;

	for (i = 0; i < ndskeys; i++)
		if (strcmp(dskeys[i].key, key) == 0) {
			*val = dskeys[i].val;
			return OK;
		}
	return ESRCH;
}

int
ds_retrieve_label_endpt(const char *name, endpoint_t *ep)
{
	(void)name;
	(void)ep;
	return ESRCH;
}

/*
 * SYS_UMAP_REMOTE (mali_umap.c на машине): как sys_umap(), но в памяти
 * клиента - у модели память всех одна, арена; только непрерывный диапазон.
 */
int
mali_umap_remote(endpoint_t ep, vir_bytes addr, vir_bytes len,
	phys_bytes *phys)
{
	size_t off = (unsigned char *)addr - cpu_view, k;

	(void)ep;
	if (alloc_of((void *)addr) == NULL)
		return EFAULT;
	for (k = PAGE; k < len + off % PAGE; k += PAGE)
		if (v2p[(off + k) / PAGE] != v2p[off / PAGE] + (int)(k / PAGE))
			return EFAULT;
	*phys = (phys_bytes)model_pa((void *)addr);
	return OK;
}

void
model_reset(void)
{
	ndskeys = 0;
	if (cpu_view == NULL) {
		if (posix_memalign((void **)&cpu_view, PAGE,
		    (size_t)ARENA_PAGES * PAGE) != 0 ||
		    posix_memalign((void **)&shadow, PAGE,
		    (size_t)ARENA_PAGES * PAGE) != 0 ||
		    posix_memalign((void **)&ram_view, PAGE,
		    (size_t)ARENA_PAGES * PAGE) != 0)
			exit(2);
	}
	memset(cpu_view, 0, (size_t)ARENA_PAGES * PAGE);
	memset(shadow, 0, (size_t)ARENA_PAGES * PAGE);
	memset(ram_view, 0, (size_t)ARENA_PAGES * PAGE);
	memset(v2p, 0, sizeof(v2p));
	memset(live, 0, sizeof(live));
	vnext = scatter_next = 0;
	contig_next = SCATTER_PAGES;
	nallocs = 0;
	ncops = 0;
	memset(model_regs, 0, sizeof(model_regs));
	memset(tlb, 0, sizeof(tlb));
	memset(tlb_next, 0, sizeof(tlb_next));
	memset(active_tt, 0, sizeof(active_tt));
	memset(locked, 0, sizeof(locked));
	memset(ncmd, 0, sizeof(ncmd));
	nflushes = 0;
	memset(sl, 0, sizeof(sl));
	fail_at_once = 0;
	lines_job = lines_mmu = 0;
	now_ticks = 0;
	timers = NULL;
	ngrants = 0;
	nreplies = 0;
	nremaps = 0;
	model_user = 0;
	vircopy_foreign = 0;
	model_recovers = 0;
	model_errors = 0;
	model_error_text[0] = '\0';
	log_clear();

	memset(&mali, 0, sizeof(mali));
	mali.info.gpu.base = 0xfde60000;
	mali.info.gpu.size = sizeof(model_regs);
	mali.info.gpu.v = (vir_bytes)model_regs;
	mali.info.irq[MALI_IRQ_GPU] = 71;
	mali.info.irq[MALI_IRQ_JOB] = 72;
	mali.info.irq[MALI_IRQ_MMU] = 73;
	mali.shader_present = 1;
	mali.as_present = 0xff;
	mali.js_present = 7;
}

/* ---- дескрипторы: cdevfd(2) VFS, открытые minor libchardriver ------------ */

/*
 * Таблицы дескрипторов процессов, как их держит VFS: дескриптор - файл
 * (filp), общий у dup, fork и передачи через сокет; последнее закрытие
 * файла устройства службы - закрытие его minor (cdr_close()).  Дать или
 * назвать дескриптор можно только процессу, от которого идёт вызов
 * (model_user): это проверка VFS, что процесс ждёт этого драйвера.
 * libchardriver не отвечает на запрос к minor, которого не видел
 * открытым: модель требует chardriver_open_minor() до cdevfd_new().
 */
#define MFILES		1024
#define MPROCS		8
#define MFDS		256

static struct mfile {
	unsigned count;			/* дескрипторов на него; 0 - свободен */
	int minor;			/* -1: файл чужого устройства (труба) */
	int flags;
	uint64_t size;
} mfiles[MFILES];

static struct {
	endpoint_t who;
	int fd[MFDS];			/* номер файла + 1, или 0 */
} mprocs[MPROCS];

static int open_minors[512];
static unsigned nopen_minors;
static unsigned sel_reply[4096];	/* последнее уведомление select */
int (*model_cdev_close)(devminor_t minor);

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
	model_error("too many processes in the model");
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
	/* libchardriver помнит 256, и 17 из них - контексты и узел. */
	if (nopen_minors >= 256 - 17)
		return -ENFILE;
	open_minors[nopen_minors++] = minor;
	return OK;
}

int
model_minor_seen(devminor_t minor)
{
	unsigned i;

	for (i = 0; i < nopen_minors; i++)
		if (open_minors[i] == minor)
			return 1;
	return 0;
}

unsigned
model_minors_seen(void)
{
	return nopen_minors;
}

int
cdevfd_new(endpoint_t endpt, devminor_t minor, int flags, uint64_t size)
{
	int f, fd;

	if (endpt != model_user) {
		model_error("cdevfd_new for %d, which does not wait on the "
		    "driver (%d does)", endpt, model_user);
		return -EPERM;
	}
	if (!model_minor_seen(minor))
		model_error("cdevfd_new of minor %d that libchardriver never "
		    "saw opened: its requests would be dropped", minor);
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

	if (endpt != model_user) {
		model_error("cdevfd_get for %d, which does not wait on the "
		    "driver (%d does)", endpt, model_user);
		return -EPERM;
	}
	if ((f = mfd_file(endpt, fd)) < 0)
		return -EBADF;
	if (mfiles[f].minor < 0)
		return -EINVAL;
	return mfiles[f].minor;
}

/* Дескриптор чужого устройства (труба) у процесса who. */
int
model_fd_foreign(endpoint_t who)
{
	int f;

	for (f = 0; f < MFILES && mfiles[f].count != 0; f++)
		;
	mfiles[f].minor = -1;
	mfiles[f].flags = O_RDWR;
	mfiles[f].size = 0;
	return mfd_install(who, f);
}

/* Тот же файл у процесса to (dup, fork, SCM_RIGHTS): новый дескриптор. */
int
model_fd_pass(endpoint_t from, int fd, endpoint_t to)
{
	int f = mfd_file(from, fd);

	if (f < 0) {
		model_error("pass of a closed descriptor %d of %d", fd, from);
		return -1;
	}
	return mfd_install(to, f);
}

/* close(2): последний дескриптор файла службы - закрытие minor. */
int
model_fd_close(endpoint_t who, int fd)
{
	int p = mproc(who), f = mfd_file(who, fd);

	if (f < 0)
		return EBADF;
	mprocs[p].fd[fd] = 0;
	if (--mfiles[f].count == 0 && mfiles[f].minor >= 0) {
		if (model_cdev_close == NULL)
			model_error("no device to close minor %d on",
			    mfiles[f].minor);
		else
			(void)model_cdev_close(mfiles[f].minor);
	}
	return OK;
}

/* Процесс уходит: все его дескрипторы закрываются. */
void
model_proc_exit(endpoint_t who)
{
	int p = mproc(who), fd;

	for (fd = 0; fd < MFDS; fd++)
		if (mprocs[p].fd[fd] != 0)
			(void)model_fd_close(who, fd);
	mprocs[p].who = 0;
}

int
model_fd_minor(endpoint_t who, int fd)
{
	int f = mfd_file(who, fd);

	return f < 0 ? -1 : mfiles[f].minor;
}

int
model_fd_flags(endpoint_t who, int fd)
{
	int f = mfd_file(who, fd);

	return f < 0 ? -1 : mfiles[f].flags;
}

/* lseek(fd, 0, SEEK_END), как VFS: размер, который дал драйвер. */
int64_t
model_fd_size(endpoint_t who, int fd)
{
	int f = mfd_file(who, fd);

	return f < 0 ? -1 : (int64_t)mfiles[f].size;
}

/* Открытых файлов службы во всех процессах. */
unsigned
model_files_open(void)
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
	if (endpt != VFS_EP)
		model_error("a select reply to %d, not VFS", endpt);
	if (minor < 0 || minor >= 4096)
		model_error("a select reply for minor %d", minor);
	else
		sel_reply[minor] |= (unsigned)ops;
}

/* Что служба сказала о select на minor с прошлого раза. */
unsigned
model_select_reply(devminor_t minor)
{
	unsigned r = sel_reply[minor];

	sel_reply[minor] = 0;
	return r;
}

void
model_fds_reset(void)
{
	memset(mfiles, 0, sizeof(mfiles));
	memset(mprocs, 0, sizeof(mprocs));
	memset(sel_reply, 0, sizeof(sel_reply));
	nopen_minors = 0;
	model_cdev_close = NULL;
}

/* Дескриптор minor устройства службы, как его сделал open(2) (контекст). */
int
model_fd_device(endpoint_t who, devminor_t minor)
{
	int f;

	for (f = 0; f < MFILES && mfiles[f].count != 0; f++)
		;
	mfiles[f].minor = minor;
	mfiles[f].flags = O_RDWR;
	mfiles[f].size = 0;
	return mfd_install(who, f);
}
