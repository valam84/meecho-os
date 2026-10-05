/*
 * fpstress: do the FP/SIMD registers of a process survive being preempted,
 * switched out and migrated?
 *
 * Each pass fills v0..v31 with a pattern of its own (seed + register number,
 * both lanes), spins long enough to be preempted - by the timer, by another
 * CPU's wake-up IPI, by sched moving it - and then checks every register.
 * Nothing between the fill and the check is C, so a compiler has no reason to
 * touch them; a changed register is the kernel's doing.
 *
 * Written for the VFS assertion of 2026-10-05 (port/PORTING-LOG.md): VFS
 * stores a pair of lock values from v31 one instruction after setting it,
 * and once found both zero.
 *
 *	fpstress [passes] [spin]
 *
 * Prints "fpstress: ok <passes>" or the first bad register and exits 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#define FILL(n)	"dup v" #n ".2d, x9\n\tadd x9, x9, #1\n\t"
#define CHECK(n) \
	"umov x11, v" #n ".d[0]\n\tcmp x11, x9\n\tb.ne 9f\n\t" \
	"umov x11, v" #n ".d[1]\n\tcmp x11, x9\n\tb.ne 9f\n\t" \
	"add x9, x9, #1\n\tadd x12, x12, #1\n\t"

static long
one_pass(uint64_t seed, uint64_t spin, uint64_t *got)
{
	long bad;
	uint64_t val;

	__asm__ volatile(
	"mov x9, %[seed]\n\t"
	FILL(0) FILL(1) FILL(2) FILL(3) FILL(4) FILL(5) FILL(6) FILL(7)
	FILL(8) FILL(9) FILL(10) FILL(11) FILL(12) FILL(13) FILL(14) FILL(15)
	FILL(16) FILL(17) FILL(18) FILL(19) FILL(20) FILL(21) FILL(22) FILL(23)
	FILL(24) FILL(25) FILL(26) FILL(27) FILL(28) FILL(29) FILL(30) FILL(31)
	"mov x10, %[spin]\n"
	"1:\tsubs x10, x10, #1\n\tb.ne 1b\n\t"
	"mov x9, %[seed]\n\tmov x12, #0\n\t"
	CHECK(0) CHECK(1) CHECK(2) CHECK(3) CHECK(4) CHECK(5) CHECK(6) CHECK(7)
	CHECK(8) CHECK(9) CHECK(10) CHECK(11) CHECK(12) CHECK(13) CHECK(14)
	CHECK(15) CHECK(16) CHECK(17) CHECK(18) CHECK(19) CHECK(20) CHECK(21)
	CHECK(22) CHECK(23) CHECK(24) CHECK(25) CHECK(26) CHECK(27) CHECK(28)
	CHECK(29) CHECK(30) CHECK(31)
	"mov x12, #-1\n\tmov x11, #0\n"
	"9:\tmov %[bad], x12\n\tmov %[val], x11\n\t"
	: [bad] "=r" (bad), [val] "=r" (val)
	: [seed] "r" (seed), [spin] "r" (spin)
	: "x9", "x10", "x11", "x12", "cc", "memory",
	  "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
	  "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
	  "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
	  "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31");

	*got = val;
	return bad;
}

int
main(int argc, char **argv)
{
	long passes = argc > 1 ? atol(argv[1]) : 2000;
	uint64_t spin = argc > 2 ? strtoull(argv[2], NULL, 0) : 200000;
	uint64_t seed = ((uint64_t)getpid() << 32) | 0x5a5a0000;
	long i, bad;
	uint64_t got;

	for (i = 0; i < passes; i++, seed += 64) {
		bad = one_pass(seed, spin, &got);
		if (bad >= 0) {
			char line[160];
			int fd, n;

			n = snprintf(line, sizeof(line), "fpstress: pid %d "
			    "pass %ld: v%ld is %#llx, expected %#llx\n",
			    (int)getpid(), i, bad, (unsigned long long)got,
			    (unsigned long long)(seed + bad));
			fputs(line, stdout);
			/*
			 * And on the console: if the same fault takes a server
			 * down, the ssh session carrying stdout goes with it.
			 */
			if ((fd = open("/dev/console", O_WRONLY)) >= 0) {
				(void)write(fd, line, n);
				close(fd);
			}
			return 1;
		}
	}
	printf("fpstress: ok %ld\n", passes);
	return 0;
}
