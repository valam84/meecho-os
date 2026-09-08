/* Utilities to generate a proper C stack.
 *
 * Author: Lionel A. Sambuc. 
 */

#define _MINIX_SYSTEM

#include <sys/cdefs.h>
#include "namespace.h"
#include <lib.h>

#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <sys/exec_elf.h>
#include <sys/exec.h>

/* Create a stack image that only needs to be patched up slightly by
 * the kernel to be used for the process to be executed.
 *
 * Every pointers are stored here as offset from the frame base, and
 * will be adapted as required for the new process address space.
 *
 * The following parameters are passed by register to either __start
 * for static binaries, or _rtld_start for dynamic ones:
 *     *fct, *ObjEntry, *ps_string
 *
 * The following stack layout is expected by _rtld():
 *
 * | XXXXXXXXXX | 0x0000_00000
 * |  ...       |
 * |  ...       | Top of the stack
 * | argc       |
 * | *argv1     | points to the first char of the argv1
 * |  ...       |
 * | *argvN     |
 * | NULL       |
 * | *env1      | 
 * |  ...       |
 * | *envN      |
 * | NULL       |
 * | ElfAuxV1   |
 * |  ...       |
 * | ElfAuxVX   |
 * | AuxExecName| fully resolve executable name, as an ASCIIZ string,
 *                at most PMEF_EXECNAMELEN1 long.
 * 
 * Here we put first the strings, then padding, then ps_strings, to
 * comply with the expected layout of NetBSD. This seems to matter for
 * the NetBSD ps command, so let's make sure we are compatible...
 *
 * The padding is however much the frame's alignment leaves, not a word's
 * worth: ps_strings is the last sizeof(struct ps_strings) bytes of the
 * frame, and that is a contract, not a coincidence. VFS finds it there when
 * it patches the frame for a #! script and when it reads it for the ELF aux
 * vectors, and the address it then hands the new process is computed from
 * the end of the frame too.
 *
 * | strings    | Followed by padding up to ps_strings.
 * | **argv     | \
 * | argc       |  +---> ps_string structure content.
 * | **env      |  |
 * | envc       | /
 * | sigcode    | On NetBSD, there may be a compatibility stub here,
 * +------------+    for native code, it is not present.
 *   Stack Base , 0xF000_0000, descending stack.
 */

/* The minimum size of the frame is composed of:
 * argc, the NULL terminator for argv as well as one for
 * environ, the ELF Aux vectors, executable name and the
 * ps_strings struct.
 *
 * argc is counted as a stack word, not as an int: it occupies a whole slot
 * at the top of the stack, ahead of the argv pointers. The two are the same
 * width on a 32-bit machine, which is why this said sizeof(int) for years. */
#define STACK_MIN_SZ \
( \
	sizeof(void *) + sizeof(void *) * 2 + \
	sizeof(AuxInfo) * PMEF_AUXVECTORS + PMEF_EXECNAMELEN1 + \
	sizeof(struct ps_strings) \
)

/*
 * What the initial stack pointer has to be aligned to.
 *
 * A stack word was enough for as long as every MINIX was 32-bit, and the
 * frame size below was rounded to sizeof(void *) for that reason. AArch64
 * makes it architecture: SP must be a multiple of 16 whenever it is used as
 * a base register, and the procedure call standard is written on that
 * assumption throughout.
 *
 * Getting it wrong does not fault. The stack pointer starts eight bytes low,
 * every frame the process builds is eight bytes low with it, prologues and
 * epilogues still agree with each other - and then one routine that aligns
 * the stack itself, or one hand-written epilogue, is off by a word and the
 * process returns to whatever was next to its saved link register. That was
 * three of the boot servers returning to address zero, with nothing in the
 * fault to say the stack had been misaligned since exec.
 */
#define STACK_ALIGN	PMEF_STACK_ALIGN

/***************************************************************************** 
 * Computes stack size, argc, envc, for a given set of path, argv, envp.     *
 *****************************************************************************/
void minix_stack_params(const char *path, char * const *argv, char * const *envp,
	size_t *stack_size,  char *overflow, int *argc, int *envc)
{
	char * const *p;
	size_t const min_size = STACK_MIN_SZ;

	*stack_size = min_size;	/* Size of the new initial stack. */
	*overflow = 0;		/* No overflow yet. */
	*argc = 0;		/* Argument count. */
	*envc = 0;		/* Environment count */

	/* Compute and add the size required to store argv and env. */
	for (p = argv; *p != NULL; p++) {
		size_t const n = sizeof(*p) + strlen(*p) + 1;
		*stack_size += n;
		if (*stack_size < n) {
			*overflow = 1;
		}
		(*argc)++;
	}

	for (p = envp; p && *p != NULL; p++) {
		size_t const n = sizeof(*p) + strlen(*p) + 1;
		*stack_size += n;
		if (*stack_size < n) {
			*overflow = 1;
		}
		(*envc)++;
	}

	/* Compute the aligned frame size. */
	*stack_size = (*stack_size + STACK_ALIGN - 1) &
		 ~((size_t)STACK_ALIGN - 1);

	if (*stack_size < min_size) {
		/* This is possible only in case of overflow. */
		*overflow = 1;
	}
}

/*****************************************************************************
 * Generate a stack in the buffer frame, ready to be used.                   *
 *****************************************************************************/
void minix_stack_fill(const char *path, int argc, char * const *argv,
	int envc, char * const *envp, size_t stack_size, char *frame,
	vir_bytes *vsp, struct ps_strings **psp)
{
	char * const *p;

	/* Frame pointers (a.k.a stack pointer within the buffer in current
	 * address space.) */
	char *fp;	/* byte aligned */
	char **fpw;	/* word aligned */

	size_t const min_size = STACK_MIN_SZ;

	/* Virtual address of the stack pointer, in new memory space. */
	*vsp = minix_get_user_sp() - stack_size;

	/* Fill in the frame now. */
	fpw = (char **) frame;
	*fpw++ = (char *)(uintptr_t) argc;

	/* The strings themselves are stored after the aux vectors,
	 * cf. top comment. */
	fp = frame + (min_size - sizeof(struct ps_strings)) + 
		(envc + argc) * sizeof(char *);
	
	/* Fill in argv and the environment, as well as copy the strings
	 * themselves. */
	for (p = argv; *p != NULL; p++) {
		size_t const n = strlen(*p) + 1;
		*fpw++= (char *)(*vsp + (fp - frame));
		memcpy(fp, *p, n);
		fp += n;
	}
	*fpw++ = NULL;

	for (p = envp; p && *p != NULL; p++) {
		size_t const n = strlen(*p) + 1;
		*fpw++= (char *)(*vsp + (fp - frame));
		memcpy(fp, *p, n);
		fp += n;
	}
	*fpw++ = NULL;

	/*
	 * Padding, up to where ps_strings goes: the last thing in the frame,
	 * by the contract in the comment at the top.  It used to be padded to
	 * a word here while the frame was rounded to STACK_ALIGN above, and
	 * on AArch64 those differ: half the time, depending on nothing but
	 * the total length of the strings, the structure ended eight bytes
	 * short of the frame and VFS - which takes the structure from the end
	 * of the frame - patched the eight bytes after it instead when it
	 * inserted the interpreter of a #! script.  What that did to the real
	 * structure was to add one to ps_envstr, the pointer crt0 makes
	 * environ from, so the interpreter read its environment one byte off
	 * and died in getenv() before main().  A shell script that ran or did
	 * not depending on how long its environment happened to be.
	 */
	{
		char *end = frame + stack_size - sizeof(struct ps_strings);

		memset(fp, 0, (size_t)(end - fp));
		fp = end;
	}

	/* Fill in the ps_string struct*/
	*psp = (struct ps_strings *) fp;

	/* argv starts one stack word in, after the argc slot written above. */
	(*psp)->ps_argvstr = (char **)(*vsp + sizeof(char *));
	(*psp)->ps_nargvstr = argc;
	(*psp)->ps_envstr = (*psp)->ps_argvstr + argc + 1;
	(*psp)->ps_nenvstr = envc;
}
