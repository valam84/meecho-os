/* This file is concerned with allocating and freeing arbitrary-size blocks of
 * physical memory.
 */

#define _SYSTEM 1

#include <minix/com.h>
#include <minix/callnr.h>
#include <minix/type.h>
#include <minix/config.h>
#include <minix/const.h>
#include <minix/sysutil.h>
#include <minix/syslib.h>
#include <minix/debug.h>
#include <minix/bitmap.h>

#include <sys/mman.h>

#include <limits.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <memory.h>

#include "vm.h"
#include "proto.h"
#include "util.h"
#include "glo.h"
#include "sanitycheck.h"
#include "memlist.h"

/* Number of physical pages in a 32-bit address space */
#define NUMBER_PHYSICAL_PAGES (int)(0x100000000ULL/VM_PAGE_SIZE)
#define PAGE_BITMAP_CHUNKS BITMAP_CHUNKS(NUMBER_PHYSICAL_PAGES)
static bitchunk_t free_pages_bitmap[PAGE_BITMAP_CHUNKS];
#define PAGE_CACHE_MAX 10000
static int free_page_cache[PAGE_CACHE_MAX];
static int free_page_cache_size = 0;

/*
 * How many pages are free right now, kept alongside the bitmap so that
 * the question is one comparison and not a walk of a million bits, and
 * how many of them are held back for system processes.
 *
 * The reserve is what stands between "a user process ate the memory" and
 * "the system is gone": without it the first process to exhaust RAM took
 * VFS down with it - VFS grew its heap by one page, got nothing, faulted,
 * RS could not replace it, and the kernel stopped. A request without
 * PAF_RESERVE (a user process's pages) is refused once it would leave less
 * than the reserve, and the process gets ENOMEM or SIGSEGV - the reasonable
 * response test64 asks for. VM's own pages, and the pages of processes RS
 * has marked as system, carry PAF_RESERVE and may use it up. Sized in
 * mem_init(): 2% of RAM, between 2 and 32 MB.
 */
static phys_bytes free_pages_count = 0;
static phys_bytes reserve_pages = 0;
#define RESERVE_MIN_PAGES	((2 * 1024 * 1024) / VM_PAGE_SIZE)
#define RESERVE_MAX_PAGES	((32 * 1024 * 1024) / VM_PAGE_SIZE)

/* Used for sanity check. */
static phys_bytes mem_low, mem_high;

static void free_pages(phys_bytes addr, int pages);
static phys_bytes alloc_pages(int pages, int flags);

#if SANITYCHECKS
struct {
	int used;
	const char *file;
	int line;
} pagemap[NUMBER_PHYSICAL_PAGES];
#endif

#define page_isfree(i) GET_BIT(free_pages_bitmap, i)

#define RESERVEDMAGIC		0x6e4c74d5
#define MAXRESERVEDPAGES	300
#define MAXRESERVEDQUEUES	 15

static struct reserved_pages {
	struct reserved_pages *next;	/* next in use */
	int max_available;	/* queue depth use, 0 if not in use at all */
	int npages;		/* number of consecutive pages */
	int mappedin;		/* must reserved pages also be mapped? */
	int n_available;	/* number of queue entries */
	int allocflags;		/* allocflags for alloc_mem */
	struct reserved_pageslot {
		phys_bytes	phys;
		void		*vir;
	} slots[MAXRESERVEDPAGES];
	u32_t magic;
} reservedqueues[MAXRESERVEDQUEUES], *first_reserved_inuse = NULL;

int missing_spares = 0;

static void sanitycheck_queues(void)
{
	struct reserved_pages *mrq;
	int m = 0;

	for(mrq = first_reserved_inuse; mrq; mrq = mrq->next) {
		assert(mrq->max_available > 0);
		assert(mrq->max_available >= mrq->n_available);
		m += mrq->max_available - mrq->n_available;
	}

	assert(m == missing_spares);
}

static void sanitycheck_rq(struct reserved_pages *rq)
{
	assert(rq->magic == RESERVEDMAGIC);
	assert(rq->n_available >= 0);
	assert(rq->n_available <= MAXRESERVEDPAGES);
	assert(rq->n_available <= rq->max_available);

	sanitycheck_queues();
}

void *reservedqueue_new(int max_available, int npages, int mapped, int allocflags)
{
	int r;
	struct reserved_pages *rq;

	assert(max_available > 0);
	assert(max_available < MAXRESERVEDPAGES);
	assert(npages > 0);
	assert(npages < 10);

	for(r = 0; r < MAXRESERVEDQUEUES; r++)
		if(!reservedqueues[r].max_available)
			break;

	if(r >= MAXRESERVEDQUEUES) {
		printf("VM: %d reserved queues in use\n", MAXRESERVEDQUEUES);
		return NULL;
	}

	rq = &reservedqueues[r];

	memset(rq, 0, sizeof(*rq));
	rq->next = first_reserved_inuse;
	first_reserved_inuse = rq;

	rq->max_available = max_available;
	rq->npages = npages;
	rq->mappedin = mapped;
	rq->allocflags = allocflags;
	rq->magic = RESERVEDMAGIC;

	missing_spares += max_available;

	return rq;
}

static void
reservedqueue_fillslot(struct reserved_pages *rq,
	struct reserved_pageslot *rps, phys_bytes ph, void *vir)
{
	rps->phys = ph;
	rps->vir = vir;
	assert(missing_spares > 0);
	if(rq->mappedin) assert(vir);
	missing_spares--;
	rq->n_available++;
}

static int
reservedqueue_addslot(struct reserved_pages *rq)
{
	phys_bytes cl, cl_addr;
	void *vir;
	struct reserved_pageslot *rps;

	sanitycheck_rq(rq);

	if((cl = alloc_mem(rq->npages, rq->allocflags | PAF_RESERVE)) == NO_MEM)
		return ENOMEM;

	cl_addr = CLICK2ABS(cl);

	vir = NULL;

	if(rq->mappedin) {
		if(!(vir = vm_mappages(cl_addr, rq->npages))) {
			free_mem(cl, rq->npages);
			printf("reservedqueue_addslot: vm_mappages failed\n");
			return ENOMEM;
		}
	}

	rps = &rq->slots[rq->n_available];

	reservedqueue_fillslot(rq, rps, cl_addr, vir);

	return OK;
}

void reservedqueue_add(void *rq_v, void *vir, phys_bytes ph)
{
	struct reserved_pages *rq = rq_v;
	struct reserved_pageslot *rps;

	sanitycheck_rq(rq);

	rps = &rq->slots[rq->n_available];

	reservedqueue_fillslot(rq, rps, ph, vir);
}

static int reservedqueue_fill(void *rq_v)
{
	struct reserved_pages *rq = rq_v;
	int r;

	sanitycheck_rq(rq);

	while(rq->n_available < rq->max_available)
		if((r=reservedqueue_addslot(rq)) != OK)
			return r;

	return OK;
}

int
reservedqueue_alloc(void *rq_v, phys_bytes *ph, void **vir)
{
	struct reserved_pages *rq = rq_v;
	struct reserved_pageslot *rps;

	sanitycheck_rq(rq);

	if(rq->n_available < 1) return ENOMEM;

	rq->n_available--;
	missing_spares++;
	rps = &rq->slots[rq->n_available];

	*ph = rps->phys;
	*vir = rps->vir;

	sanitycheck_rq(rq);

	return OK;
}

void alloc_cycle(void)
{
	struct reserved_pages *rq;
	sanitycheck_queues();
	for(rq = first_reserved_inuse; rq && missing_spares > 0; rq = rq->next) {
		sanitycheck_rq(rq);
		reservedqueue_fill(rq);
		sanitycheck_rq(rq);
	}
	sanitycheck_queues();
}

/*===========================================================================*
 *				alloc_mem				     *
 *===========================================================================*/
phys_clicks alloc_mem(phys_clicks clicks, u32_t memflags)
{
/* Allocate a block of memory from the free list using first fit. The block
 * consists of a sequence of contiguous bytes, whose length in clicks is
 * given by 'clicks'.  A pointer to the block is returned.  The block is
 * always on a click boundary.  This procedure is called when memory is
 * needed for FORK or EXEC.
 */
  phys_clicks mem = NO_MEM, align_clicks = 0;

  if(memflags & PAF_ALIGN64K) {
  	align_clicks = (64 * 1024) / CLICK_SIZE;
	clicks += align_clicks;
  } else if(memflags & PAF_ALIGN16K) {
	align_clicks = (16 * 1024) / CLICK_SIZE;
	clicks += align_clicks;
  }

  do {
	mem = alloc_pages(clicks, memflags);
  } while(mem == NO_MEM && cache_freepages(clicks) > 0);

  if(mem == NO_MEM)
  	return mem;

  if(align_clicks) {
  	phys_clicks o;
  	o = mem % align_clicks;
  	if(o > 0) {
  		phys_clicks e;
  		e = align_clicks - o;
	  	free_mem(mem, e);
	  	mem += e;
	}
  }

  return mem;
}

void mem_add_total_pages(int pages)
{
	total_pages += pages;
}

/*===========================================================================*
 *				free_mem				     *
 *===========================================================================*/
void free_mem(phys_clicks base, phys_clicks clicks)
{
/* Return a block of free memory to the hole list.  The parameters tell where
 * the block starts in physical memory and how big it is.  The block is added
 * to the hole list.  If it is contiguous with an existing hole on either end,
 * it is merged with the hole or holes.
 */
  if (clicks == 0) return;

  assert(CLICK_SIZE == VM_PAGE_SIZE);
  free_pages(base, clicks);
  return;
}

/*===========================================================================*
 *				mem_init				     *
 *===========================================================================*/
void mem_init(struct memory *chunks)
{
/* Initialize hole lists.  There are two lists: 'hole_head' points to a linked
 * list of all the holes (unused memory) in the system; 'free_slots' points to
 * a linked list of table entries that are not in use.  Initially, the former
 * list has one entry for each chunk of physical memory, and the second
 * list links together the remaining table slots.  As memory becomes more
 * fragmented in the course of time (i.e., the initial big holes break up into
 * smaller holes), new table slots are needed to represent them.  These slots
 * are taken from the list headed by 'free_slots'.
 */
  int i, first = 0;

  total_pages = 0;

  memset(free_pages_bitmap, 0, sizeof(free_pages_bitmap));

  /* Use the chunks of physical memory to allocate holes. */
  for (i=NR_MEMS-1; i>=0; i--) {
  	if (chunks[i].size > 0) {
		phys_bytes from = CLICK2ABS(chunks[i].base),
			to = CLICK2ABS(chunks[i].base+chunks[i].size)-1;
		if(first || from < mem_low) mem_low = from;
		if(first || to > mem_high) mem_high = to;
		free_mem(chunks[i].base, chunks[i].size);
		total_pages += chunks[i].size;
		first = 0;
	}
  }

  /* The system reserve: see the note at free_pages_count. */
  reserve_pages = total_pages / 50;
  if(reserve_pages < RESERVE_MIN_PAGES) reserve_pages = RESERVE_MIN_PAGES;
  if(reserve_pages > RESERVE_MAX_PAGES) reserve_pages = RESERVE_MAX_PAGES;
}

/*
 * For /proc and diagnostics: the reserve as set, and what is free now.
 */
void mem_reserve_stats(phys_bytes *free, phys_bytes *reserve)
{
	*free = free_pages_count;
	*reserve = reserve_pages;
}

/*
 * Whom VM's own memory is for.
 *
 * VM's own pages - its heap, its slabs, the page tables it keeps for every
 * process - used to carry PAF_RESERVE always: they are VM's. But most of
 * them VM spends on somebody's behalf, and how much a user process makes
 * it spend is the user's choice: lazy gigabytes of mmap() cost megabytes
 * of physblocks arrays, a fork of a sparsely touched 4 GiB costs the child
 * up to 8 MB of page tables, and neither touches one page of the user's.
 * Taken from the reserve, that is the reserve spent by a user - which is
 * what the reserve exists to prevent.
 *
 * So VM's work is charged to the process it is for: mem_charge() is called
 * where that is known (the caller of a call, the process whose memory a
 * fault or a copy touches, the process PM or VFS asks about), the main
 * loop clears it, and mem_own_flags() gives VM's own allocations
 * PAF_RESERVE only when the work is not a user process's. What such an
 * allocation refuses, the user gets as ENOMEM or SIGSEGV - where its own
 * pages are refused anyway.
 */
static int charged_to_user = 0;

void mem_charge(struct vmproc *vmp)
{
	charged_to_user = vmp != NULL && !acl_is_system(vmp);
}

int mem_charged_to_user(void)
{
	return charged_to_user;
}

/* Work that must not fail once begun: back on the system's account. */
int mem_charge_system(void)
{
	int was = charged_to_user;

	charged_to_user = 0;
	return was;
}

void mem_charge_restore(int was)
{
	charged_to_user = was;
}

u32_t mem_own_flags(void)
{
	return charged_to_user ? 0 : PAF_RESERVE;
}

/*
 * Whether 'bytes' could still be had on a user's terms: outside the
 * reserve, the page cache given up first, as alloc_pages() does.
 */
int mem_user_room(phys_bytes bytes)
{
	phys_bytes pages = (bytes + VM_PAGE_SIZE - 1) / VM_PAGE_SIZE;

	do {
		if(free_pages_count >= reserve_pages + pages)
			return 1;
	} while(cache_freepages(pages) > 0);

	return 0;
}

#if SANITYCHECKS
void mem_sanitycheck(const char *file, int line)
{
	int i;
	for(i = 0; i < NUMBER_PHYSICAL_PAGES; i++) {
		if(!page_isfree(i)) continue;
		MYASSERT(usedpages_add(i * VM_PAGE_SIZE, VM_PAGE_SIZE) == OK);
	}
}
#endif

/*
 * The free runs of the bitmap: how many, how many pages, the longest.
 * Walked a chunk at a time - a chunk with no page free or every page free
 * is one step - and bit by bit only where the two meet.  The bitmap covers
 * the whole 32-bit physical space, a million pages, and the walk bit by bit
 * cost VM 7 to 10 ms a call on the Mi Pad 4's little cores; /proc/meminfo
 * asks for it on every read(2), and a shell's "read" reads a byte at a
 * time - the desktop's panel read it so every two seconds, and that was
 * most of VM's time on an idle desktop (port/clover/hw-cpu.md, "Фон на
 * ядре 0").  Same answers; the chunks tile the bitmap exactly, and one
 * past the end reads as all used, which ends the last run.
 */
void memstats(int *nodes, int *pages, int *largest)
{
	int c, b, run = 0;
	bitchunk_t w;

	*nodes = 0;
	*pages = 0;
	*largest = 0;

	for (c = 0; c <= PAGE_BITMAP_CHUNKS; c++) {
		w = c < PAGE_BITMAP_CHUNKS ? free_pages_bitmap[c] : 0;
		if (w == (bitchunk_t)~(bitchunk_t)0) {
			run += BITCHUNK_BITS;
			continue;
		}
		for (b = 0; b < (int)BITCHUNK_BITS; b++) {
			if (w & ((bitchunk_t)1 << b)) {
				run++;
				continue;
			}
			if (run > 0) {
				(*nodes)++;
				*pages += run;
				if (run > *largest)
					*largest = run;
				run = 0;
			}
			if (w == 0)	/* nothing free in the chunk */
				break;
		}
	}
}

/*
 * Returns a page number, or NO_MEM. It has to return phys_bytes and not int:
 * NO_MEM is (phys_clicks) 0xFFFFFFFE, and through an int return it came back
 * as -2, which widens to 0xFFFFFFFFFFFFFFFE on LP64 - not NO_MEM. alloc_pages()
 * then took page -2 for a real block and cleared bits far outside the bitmap,
 * and the first process to exhaust memory (test64) took VM down with a wild
 * store. On ILP32 the -2 wrapped back to 0xFFFFFFFE and nothing showed.
 */
static phys_bytes findbit(int low, int startscan, int pages, int memflags, int *len)
{
	int run_length = 0, i;
	int freerange_start = startscan;

	for(i = startscan; i >= low; i--) {
		if(!page_isfree(i)) {
			int pi;
			int chunk = i/BITCHUNK_BITS, moved = 0;
			run_length = 0;
			pi = i;
			while(chunk > 0 &&
			   !MAP_CHUNK(free_pages_bitmap, chunk*BITCHUNK_BITS)) {
				chunk--;
				moved = 1;
			}
			if(moved) { i = chunk * BITCHUNK_BITS + BITCHUNK_BITS; }
			continue;
		}
		if(!run_length) { freerange_start = i; run_length = 1; }
		else { freerange_start--; run_length++; }
		assert(run_length <= pages);
		if(run_length == pages) {
			/* good block found! */
			*len = run_length;
			return freerange_start;
		}
	}

	return NO_MEM;
}

/*===========================================================================*
 *				alloc_pages				     *
 *===========================================================================*/
static phys_bytes alloc_pages(int pages, int memflags)
{
	phys_bytes boundary16 = 16 * 1024 * 1024 / VM_PAGE_SIZE;
	phys_bytes boundary1  =  1 * 1024 * 1024 / VM_PAGE_SIZE;
	phys_bytes mem = NO_MEM, i;	/* page number */
	int maxpage = NUMBER_PHYSICAL_PAGES - 1;
	static int lastscan = -1;
	int startscan, run_length;

	/*
	 * The system reserve: see the note at free_pages_count. Checked
	 * before any search, the page cache path included.
	 */
	if(!(memflags & PAF_RESERVE) &&
	   free_pages_count < reserve_pages + (phys_bytes) pages)
		return NO_MEM;

	if(memflags & PAF_LOWER16MB)
		maxpage = boundary16 - 1;
	else if(memflags & PAF_LOWER1MB)
		maxpage = boundary1 - 1;
	else {
		/* no position restrictions: check page cache */
		if(pages == 1) {
			while(free_page_cache_size > 0) {
				i = free_page_cache[free_page_cache_size-1];
				if(page_isfree(i)) {
					free_page_cache_size--;
					mem = i;
					assert(mem != NO_MEM);
					run_length = 1;
					break;
				}
				free_page_cache_size--;
			}
		}
	}

	if(lastscan < maxpage && lastscan >= 0)
		startscan = lastscan;
	else	startscan = maxpage;

	if(mem == NO_MEM)
		mem = findbit(0, startscan, pages, memflags, &run_length);
	if(mem == NO_MEM)
		mem = findbit(0, maxpage, pages, memflags, &run_length);
	if(mem == NO_MEM)
		return NO_MEM;

	/*
	 * A page number the bitmap cannot hold is a defect above, not memory.
	 * Say so and refuse: the caller gets ENOMEM and VM stays up, which is
	 * the one thing a wild store into the bitmap cannot promise.
	 */
	if(mem + pages > (phys_bytes) NUMBER_PHYSICAL_PAGES) {
		printf("VM: alloc_pages: page %lu+%d is outside the bitmap\n",
			(unsigned long) mem, pages);
		return NO_MEM;
	}

	/* remember for next time */
	lastscan = mem;

	for(i = mem; i < mem + pages; i++) {
		UNSET_BIT(free_pages_bitmap, i);
	}
	assert(free_pages_count >= (phys_bytes) pages);
	free_pages_count -= pages;

	if(memflags & PAF_CLEAR) {
		int s;
		if ((s= sys_memset(NONE, 0, CLICK_SIZE*mem,
			VM_PAGE_SIZE*pages)) != OK) 
			panic("alloc_mem: sys_memset failed: %d", s);
	}

	return mem;
}

/*===========================================================================*
 *				free_pages				     *
 *===========================================================================*/
static void free_pages(phys_bytes pageno, int npages)
{
	int i, lim = pageno + npages - 1;

#if JUNKFREE
       if(sys_memset(NONE, 0xa5a5a5a5, VM_PAGE_SIZE * pageno,
               VM_PAGE_SIZE * npages) != OK)
                       panic("free_pages: sys_memset failed");
#endif

	for(i = pageno; i <= lim; i++) {
		/* Counted only if it was not free: a page freed twice must
		 * not be counted twice, whatever else is wrong with it. */
		if(!page_isfree(i))
			free_pages_count++;
		SET_BIT(free_pages_bitmap, i);
		if(free_page_cache_size < PAGE_CACHE_MAX) {
			free_page_cache[free_page_cache_size++] = i;
		}
	}
}

/*===========================================================================*
 *				printmemstats				     *
 *===========================================================================*/
void printmemstats(void)
{
	int nodes, pages, largest;
        memstats(&nodes, &pages, &largest);
        printf("%d blocks, %d pages (%lukB) free, largest %d pages (%lukB)\n",
                nodes, pages, (unsigned long) pages * (VM_PAGE_SIZE/1024),
		largest, (unsigned long) largest * (VM_PAGE_SIZE/1024));
}


#if SANITYCHECKS

/*===========================================================================*
 *				usedpages_reset				     *
 *===========================================================================*/
void usedpages_reset(void)
{
	memset(pagemap, 0, sizeof(pagemap));
}

/*===========================================================================*
 *				usedpages_add				     *
 *===========================================================================*/
int usedpages_add_f(phys_bytes addr, phys_bytes len, const char *file, int line)
{
	u32_t pagestart, pages;

	if(!incheck)
		return OK;

	assert(!(addr % VM_PAGE_SIZE));
	assert(!(len % VM_PAGE_SIZE));
	assert(len > 0);

	pagestart = addr / VM_PAGE_SIZE;
	pages = len / VM_PAGE_SIZE;

	while(pages > 0) {
		phys_bytes thisaddr;
		assert(pagestart > 0);
		assert(pagestart < NUMBER_PHYSICAL_PAGES);
		thisaddr = pagestart * VM_PAGE_SIZE;
		assert(pagestart < NUMBER_PHYSICAL_PAGES);
		if(pagemap[pagestart].used) {
			static int warnings = 0;
			if(warnings++ < 100)
				printf("%s:%d: usedpages_add: addr 0x%lx reused, first %s:%d\n",
					file, line, thisaddr, pagemap[pagestart].file, pagemap[pagestart].line);
			util_stacktrace();
			return EFAULT;
		}
		pagemap[pagestart].used = 1;
		pagemap[pagestart].file = file;
		pagemap[pagestart].line = line;
		pages--;
		pagestart++;
	}

	return OK;
}

#endif

