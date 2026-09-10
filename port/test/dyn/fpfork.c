/*
 * Do a parent and its child have FP/SIMD register files of their own?
 *
 * The lazy FP switch gives the register file to one process per CPU and
 * saves the previous owner's copy into an area of its own, which
 * p_seg.fpu_state points at.  do_fork() copies the whole struct proc to the
 * child, so unless the pointer is put back afterwards the child ends up
 * pointing at the parent's area, and from then on each hand-over overwrites
 * the other's saved copy.
 *
 * This probe makes that visible.  The parent loads a known pattern into
 * v16..v31 and then spins on integer work - no calls, no FP - for long
 * enough to be preempted many times.  The child spins on SIMD, so every
 * time it runs it takes the register file away from the parent and gives it
 * back changed.  With one save area per process the parent reads its own
 * pattern back; with one shared area it reads the child's registers.
 *
 * Nothing here is dynamic: the defect is in the kernel, not in ld.elf_so.
 * The dynamic linker only made it visible, by being the thing that ran SIMD
 * in the child of minix/tests/test2.  See PORTING-LOG.md, "Один регистр
 * SIMD".
 *
 *	aarch64-elf64-minix-gcc -O2 -Wall -o fpfork fpfork.c
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static unsigned long long out[16][2];

/* Integer-only busy work.  volatile so the compiler keeps the loop. */
static volatile unsigned long spin;

int
main(int argc, char **argv)
{
	int i, bad = 0, status, wake[2];
	pid_t pid;
	unsigned long n;
	char c;

	(void)argc;
	(void)argv;

	if (pipe(wake) < 0) {
		perror("pipe");
		return 2;
	}

	/*
	 * Touch FP before forking, the way test2 does by printing through
	 * start(): that sets MF_FPU_INITIALIZED, which the child inherits
	 * along with the rest of struct proc, and it is what makes the two
	 * of them trade a register file that is already in use.  Also
	 * flushes, so the child does not inherit our stdio.
	 */
	printf("fpfork: parent %d\n", (int)getpid());
	fflush(stdout);

	if ((pid = fork()) < 0) {
		perror("fork");
		return 2;
	}

	if (pid == 0) {
		/* Say once that we got the processor at all. */
		{ char c = 'c'; (void)write(wake[1], &c, 1); }
		/*
		 * Child: nothing but SIMD, over and over.  Each round is one
		 * chance to take the register file from the parent.  v31 is
		 * left holding a value the parent never uses, so a parent
		 * that reads it back has read the child's file.
		 */
		for (;;)
			__asm__ volatile(
			    "mov x9, #0xdead\n\t"
			    "dup v16.2d, x9\n\t"
			    "dup v24.2d, x9\n\t"
			    "dup v31.2d, x9\n\t"
			    ::: "x9", "v16", "v24", "v31");
		/* NOTREACHED: the parent kills it once it has looked. */
	}

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

	/* Integer work only: be preempted, do not touch FP while waiting. */
	for (n = 0; n < 20000000UL; n++)
		spin += n;

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
			printf("v%d clobbered: 0x%llx 0x%llx, wanted %d\n",
			    i + 16, out[i][0], out[i][1], i + 16);
			bad++;
		}

	kill(pid, SIGKILL);
	while (waitpid(pid, &status, 0) < 0)
		continue;

	/*
	 * Did the child get the processor at all?  A probe that reports "own
	 * state" because nobody ever contended for the register file reports
	 * nothing, so say so rather than pass quietly.
	 */
	fcntl(wake[0], F_SETFL, O_NONBLOCK);
	if (read(wake[0], &c, 1) != 1) {
		printf("child never ran: this run proves nothing\n");
		return 2;
	}

	printf(bad ? "FP state shared with the child\n" :
	    "FP state is the process's own\n");
	return bad != 0;
}
