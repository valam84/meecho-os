/*
 * kptr: hand the kernel a kernel address as a message buffer.
 *
 *	kptr ADDRESS
 *
 * A regression check for usercopy.S: a kernel call ("svc #KERVEC_INTR")
 * whose message pointer is a mapped kernel address. The message copies are
 * LDTR/STTR, checked against the process's own permissions, so the kernel
 * must refuse it: "wrong user pointer" on the console and SIGSEGV here.
 * Getting past the call is the failure, and the program says so.
 *
 * qemu-pan.py passes an address inside k_overflow_stacks, the emergency
 * stacks a core uses only to report its own stack overflow - nothing that
 * matters lives there should the check ever regress. See
 * port/PORTING-LOG.md, "PAN".
 */

#include <stdio.h>
#include <stdlib.h>

#include <machine/ipcconst.h>

int
main(int argc, char **argv)
{
	register unsigned long x0 __asm__("x0");

	if (argc != 2) {
		fprintf(stderr, "usage: kptr ADDRESS\n");
		return 2;
	}

	x0 = strtoul(argv[1], NULL, 0);
	printf("kptr: kernel call with the message at 0x%lx\n", x0);
	fflush(stdout);

	__asm__ volatile("svc %1" : "+r"(x0) : "i"(KERVEC_INTR)
	    : "x1", "memory");

	printf("kptr: SURVIVED - the kernel did not refuse the address\n");
	return 1;
}
