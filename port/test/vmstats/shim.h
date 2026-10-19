/* What alloc.c gives memstats(): the bitmap and its sizes, as there. */
#include <limits.h>
#include <stdint.h>
typedef uint32_t bitchunk_t;
#define VM_PAGE_SIZE		4096
#define BITCHUNK_BITS		(sizeof(bitchunk_t) * CHAR_BIT)
#define BITMAP_CHUNKS(nr)	(((nr) + BITCHUNK_BITS - 1) / BITCHUNK_BITS)
#define NUMBER_PHYSICAL_PAGES	(int)(0x100000000ULL / VM_PAGE_SIZE)
#define PAGE_BITMAP_CHUNKS	BITMAP_CHUNKS(NUMBER_PHYSICAL_PAGES)
extern bitchunk_t free_pages_bitmap[PAGE_BITMAP_CHUNKS];
#define page_isfree(i) (free_pages_bitmap[(i) / BITCHUNK_BITS] & \
	(1u << ((i) % BITCHUNK_BITS)))
void memstats(int *nodes, int *pages, int *largest);
