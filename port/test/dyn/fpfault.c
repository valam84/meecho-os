/*
 * Does the FP/SIMD register file survive a page fault?
 *
 * Load a known pattern into v16..v31, touch a page of bss that has never
 * been written - which takes a page fault into VM and back - and read the
 * registers out again.  Nothing here is dynamic and nothing here calls a
 * library function between the load and the read-back.
 */
#include <stdio.h>
#include <string.h>

static char page[64 * 4096] __attribute__((aligned(4096)));

static unsigned long long out[16][2];

int
main(int argc, char **argv)
{
	int i, bad = 0;
	volatile char *p;

	(void)argc;
	(void)argv;

	/* v16..v31 <- 16 .. 31, each duplicated into both halves. */
	__asm__ volatile(
	    "mov x9, #16\n\t" "dup v16.2d, x9\n\t"
	    "mov x9, #17\n\t" "dup v17.2d, x9\n\t"
	    "mov x9, #18\n\t" "dup v18.2d, x9\n\t"
	    "mov x9, #19\n\t" "dup v19.2d, x9\n\t"
	    "mov x9, #20\n\t" "dup v20.2d, x9\n\t"
	    "mov x9, #21\n\t" "dup v21.2d, x9\n\t"
	    "mov x9, #22\n\t" "dup v22.2d, x9\n\t"
	    "mov x9, #23\n\t" "dup v23.2d, x9\n\t"
	    "mov x9, #24\n\t" "dup v24.2d, x9\n\t"
	    "mov x9, #25\n\t" "dup v25.2d, x9\n\t"
	    "mov x9, #26\n\t" "dup v26.2d, x9\n\t"
	    "mov x9, #27\n\t" "dup v27.2d, x9\n\t"
	    "mov x9, #28\n\t" "dup v28.2d, x9\n\t"
	    "mov x9, #29\n\t" "dup v29.2d, x9\n\t"
	    "mov x9, #30\n\t" "dup v30.2d, x9\n\t"
	    "mov x9, #31\n\t" "dup v31.2d, x9\n\t"
	    ::: "x9", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
	        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31");

	/* Touch fresh bss pages: each store is a first write to that page. */
	p = page;
	for (i = 0; i < 64; i++)
		p[i * 4096] = (char)i;

	__asm__ volatile(
	    "stp q16, q17, [%0, #0]\n\t"
	    "stp q18, q19, [%0, #32]\n\t"
	    "stp q20, q21, [%0, #64]\n\t"
	    "stp q22, q23, [%0, #96]\n\t"
	    "stp q24, q25, [%0, #128]\n\t"
	    "stp q26, q27, [%0, #160]\n\t"
	    "stp q28, q29, [%0, #192]\n\t"
	    "stp q30, q31, [%0, #224]\n\t"
	    :: "r" (out) : "memory");

	for (i = 0; i < 16; i++)
		if (out[i][0] != (unsigned long long)(i + 16) ||
		    out[i][1] != (unsigned long long)(i + 16)) {
			printf("v%d clobbered: %llu %llu, wanted %d\n", i + 16,
			    out[i][0], out[i][1], i + 16);
			bad++;
		}
	printf(bad ? "FP state did not survive\n" : "FP state survived\n");
	return bad != 0;
}
