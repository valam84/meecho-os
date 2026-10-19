#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bitchunk_t free_pages_bitmap[PAGE_BITMAP_CHUNKS];

/* The walk as it was. */
static void
ref(int *nodes, int *pages, int *largest)
{
	int i, size;

	*nodes = *pages = *largest = 0;
	for (i = 0; i < NUMBER_PHYSICAL_PAGES; i++) {
		size = 0;
		while (i < NUMBER_PHYSICAL_PAGES && page_isfree(i)) {
			size++;
			i++;
		}
		if (size == 0)
			continue;
		(*nodes)++;
		*pages += size;
		if (size > *largest)
			*largest = size;
	}
}

static void
set(int from, int n, int v)
{
	int i;

	for (i = from; i < from + n && i < NUMBER_PHYSICAL_PAGES; i++)
		if (v)
			free_pages_bitmap[i / 32] |= 1u << (i % 32);
		else
			free_pages_bitmap[i / 32] &= ~(1u << (i % 32));
}

int
main(void)
{
	int k, i, a[3], b[3], bad = 0, n = 0;

	srand(1019);
	for (k = 0; k < 300; k++) {
		memset(free_pages_bitmap, k % 3 == 0 ? 0 : 0xff,
		    sizeof(free_pages_bitmap));
		switch (k % 5) {
		case 0:		/* runs of random length, random gaps */
			for (i = 0; i < NUMBER_PHYSICAL_PAGES;) {
				int len = rand() % (k % 7 == 0 ? 3 : 200);
				set(i, len, rand() & 1);
				i += len + 1;
			}
			break;
		case 1:		/* single bits */
			for (i = 0; i < 5000; i++)
				set(rand() % NUMBER_PHYSICAL_PAGES, 1, rand() & 1);
			break;
		case 2:		/* the ends */
			set(0, 1, 0);
			set(NUMBER_PHYSICAL_PAGES - 1, 1, k & 1);
			set(31, 2, 0);
			break;
		case 3:		/* whole chunks and runs across them */
			for (i = 0; i < 2000; i++)
				set((rand() % (NUMBER_PHYSICAL_PAGES / 32)) * 32 +
				    rand() % 3 - 1, 32 * (1 + rand() % 4),
				    rand() & 1);
			break;
		case 4:		/* a machine: RAM low, the rest used */
			memset(free_pages_bitmap, 0, sizeof(free_pages_bitmap));
			set(512, 400000 + k, 1);
			for (i = 0; i < 3000; i++)
				set(512 + rand() % 400000, 1 + rand() % 40, 0);
			break;
		}
		ref(&a[0], &a[1], &a[2]);
		memstats(&b[0], &b[1], &b[2]);
		n++;
		if (memcmp(a, b, sizeof(a)) != 0) {
			if (bad++ < 5)
				printf("case %d: runs %d/%d pages %d/%d "
				    "largest %d/%d\n", k, a[0], b[0], a[1], b[1],
				    a[2], b[2]);
		}
	}
	if (bad) {
		printf("FAIL %d of %d\n", bad, n);
		return 1;
	}
	printf("ok %d\n", n);
	return 0;
}
