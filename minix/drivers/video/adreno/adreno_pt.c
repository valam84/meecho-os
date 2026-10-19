/*
 * The GPU's page tables: one address space as its SMMU's context bank walks
 * it (port/clover/hw-gpu.md, §4).  The format is ARM's own LPAE stage 1 with
 * a 4 KiB granule - the processor's - which is what Linux's io-pgtable
 * "ARM_64_LPAE_S1" writes for this SMMU (GPL; the bits, not the code):
 *
 *   - 512 eight-byte entries a table; the walk starts at the level that
 *     the input width leaves (48 bits: level 0, four levels);
 *   - a table entry is type 3 at levels 0-2, a page is type 3 at level 3
 *     (unlike the Mali's tables, where a leaf is type 1 at every level);
 *   - a page has the access flag set (the SMMU would fault on the first
 *     access otherwise), is not global (nG: its ASID tags it), may be used
 *     unprivileged (AP[1]), is read-only if not writable (AP[2]), outer
 *     shareable, and has attribute index 0 - MAIR byte 0x44, normal
 *     non-cacheable memory: the GPU does not see the processor's caches.
 *
 * Tables are pages of this service's own memory (alloc_contig(): one page,
 * physically one, its address known).  The SMMU's walk does not look into
 * the processor's caches either (TCR: non-cacheable, outer shareable), so
 * every entry written is cleaned to memory before the SMMU may walk it, and
 * a new table is cleaned whole while it is still all zeros.  What the SMMU
 * may still hold in its TLB is the caller's to invalidate (adreno_smmu.c):
 * a mapping removed here is gone from memory, not from the TLB.
 */

#include "adreno_pt.h"

#include <minix/cachectl.h>
#include <minix/syslib.h>

#include <stdlib.h>
#include <string.h>

#define PT_PAGE		4096
#define PT_NENT		512

#define PTE_VALID	(1ull << 0)
#define PTE_TABLE	(3ull << 0)	/* levels 0-2 */
#define PTE_PAGE	(3ull << 0)	/* level 3 */
#define PTE_ATTRIDX(n)	((uint64_t)(n) << 2)
#define PTE_AP_UNPRIV	(1ull << 6)
#define PTE_AP_RDONLY	(1ull << 7)
#define PTE_SH_OS	(2ull << 8)
#define PTE_AF		(1ull << 10)
#define PTE_NG		(1ull << 11)
#define PTE_ADDR	0x0000fffffffff000ull

struct ad_ptab {
	uint64_t *e;			/* the page, as this service sees it */
	phys_bytes phys;
	struct ad_ptab **child;		/* the next level's; NULL at level 3 */
};

static int
clean(void *addr, size_t len)
{
	int r;

	if ((r = sys_cachectl(CACHE_CLEAN, addr, len)) != OK)
		log_warn(&PT_LOG, "cleaning a page table to memory failed: "
		    "%d\n", r);
	return r;
}

static unsigned
idx(uint64_t va, int level)
{
	return (unsigned)(va >> (39 - 9 * level)) & (PT_NENT - 1);
}

static struct ad_ptab *
table_new(int level)
{
	struct ad_ptab *t;

	if ((t = calloc(1, sizeof(*t))) == NULL)
		return NULL;
	if (level < 3 && (t->child = calloc(PT_NENT, sizeof(*t->child))) ==
	    NULL) {
		free(t);
		return NULL;
	}
	if ((t->e = alloc_contig(PT_PAGE, AC_ALIGN4K, &t->phys)) == NULL) {
		free(t->child);
		free(t);
		return NULL;
	}
	/* Zeros in memory before the SMMU can be pointed at the table. */
	memset(t->e, 0, PT_PAGE);
	if (clean(t->e, PT_PAGE) != OK) {
		free_contig(t->e, PT_PAGE);
		free(t->child);
		free(t);
		return NULL;
	}
	return t;
}

static void
table_free(struct ad_ptab *t, int level)
{
	unsigned i;

	if (t == NULL)
		return;
	if (level < 3)
		for (i = 0; i < PT_NENT; i++)
			table_free(t->child[i], level + 1);
	free(t->child);
	free_contig(t->e, PT_PAGE);
	free(t);
}

int
pt_init(struct ad_pt *pt, unsigned ias, unsigned oas)
{
	unsigned levels;

	memset(pt, 0, sizeof(*pt));
	if (ias < 25 || ias > 48 || oas < 32 || oas > 48)
		return EINVAL;
	/* 12 bits of page, 9 a level: 48 bits are four levels from 0, 39
	 * three from 1, and so on (io-pgtable-arm.c's arithmetic). */
	levels = (ias - 12 + 8) / 9;
	pt->ias = ias;
	pt->oas = oas;
	pt->start = 4 - (int)levels;
	if ((pt->root = table_new(pt->start)) == NULL)
		return ENOMEM;
	if (pt->root->phys >> oas) {
		log_warn(&PT_LOG, "page table at 0x%llx is beyond the SMMU's "
		    "%u output bits\n", (unsigned long long)pt->root->phys, oas);
		pt_free(pt);
		return EINVAL;
	}
	return OK;
}

uint64_t
pt_root(const struct ad_pt *pt)
{
	return pt->root != NULL ? pt->root->phys : 0;
}

/* The last-level table that holds va, made on the way if make. */
static struct ad_ptab *
leaf_table(struct ad_pt *pt, uint64_t va, int make)
{
	struct ad_ptab *t = pt->root, *c;
	unsigned i;
	int level;

	for (level = pt->start; level < 3; level++) {
		i = idx(va, level);
		if ((c = t->child[i]) == NULL) {
			if (!make || (c = table_new(level + 1)) == NULL)
				return NULL;
			if (c->phys >> pt->oas) {
				table_free(c, level + 1);
				return NULL;
			}
			t->child[i] = c;
			t->e[i] = ((uint64_t)c->phys & PTE_ADDR) | PTE_TABLE;
			if (clean(&t->e[i], sizeof(t->e[i])) != OK)
				return NULL;
		}
		t = c;
	}
	return t;
}

int
pt_map(struct ad_pt *pt, uint64_t va, uint64_t pa, int prot)
{
	struct ad_ptab *t;
	uint64_t pte;
	unsigned i;

	if (pt->root == NULL || (va & (PT_PAGE - 1)) || (pa & (PT_PAGE - 1)) ||
	    (va >> pt->ias) || (pa >> pt->oas) || !(prot & AD_PT_READ))
		return EINVAL;
	if ((t = leaf_table(pt, va, 1)) == NULL)
		return ENOMEM;
	i = idx(va, 3);
	if (t->e[i] & PTE_VALID)
		return EEXIST;
	pte = (pa & PTE_ADDR) | PTE_PAGE | PTE_AF | PTE_NG | PTE_SH_OS |
	    PTE_AP_UNPRIV | PTE_ATTRIDX(0);
	if (!(prot & AD_PT_WRITE))
		pte |= PTE_AP_RDONLY;
	t->e[i] = pte;
	return clean(&t->e[i], sizeof(t->e[i]));
}

/* The entry gone from memory; the SMMU's TLB is the caller's to clear. */
int
pt_unmap(struct ad_pt *pt, uint64_t va)
{
	struct ad_ptab *t;
	unsigned i;

	if (pt->root == NULL || (t = leaf_table(pt, va, 0)) == NULL)
		return ENOENT;
	i = idx(va, 3);
	if (!(t->e[i] & PTE_VALID))
		return ENOENT;
	t->e[i] = 0;
	return clean(&t->e[i], sizeof(t->e[i]));
}

void
pt_free(struct ad_pt *pt)
{
	table_free(pt->root, pt->start);
	pt->root = NULL;
}
