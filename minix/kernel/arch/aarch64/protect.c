/*
 * What the kernel sets up once it is running in the upper half, on the way
 * from kmain() to the first process.
 *
 * The file keeps the name the other two ports use, where it is about
 * protected mode and the descriptor tables that define it. There is no
 * equivalent here - AArch64 has no segmentation, and the protection the
 * kernel relies on is in the page tables pg_utils.c already built - so what
 * is left is the part of that sequence which genuinely cannot happen in
 * pre_init(): making the page table the boot process will run in, and handing
 * it to VM.
 */

#include "kernel/kernel.h"

#include <assert.h>
#include <string.h>

#include <machine/vm.h>
#include <machine/multiboot.h>

#include <sys/exec.h>
#include <libexec.h>

#include "archconst.h"
#include "arch_proto.h"
#include "kernel/proc.h"

int prot_init_done = 0;

/*===========================================================================*
 *				prot_init				     *
 *===========================================================================*/
void
prot_init(void)
{
	/*
	 * A page table for the lower half, so that VM can be loaded into it
	 * and run. Until now the lower half has held nothing: pre_init() used
	 * TTBR0 for the identity map and then switched TTBR0 walks off
	 * altogether, so that a leftover physical pointer would fault.
	 *
	 * The 32-bit ports rebuild their kernel mapping at this point too,
	 * because the one pre_init made lives in memory that is about to be
	 * freed. Here the kernel's mapping is ordinary kernel memory and sits
	 * in TTBR1, which nothing switches and nothing frees, so there is
	 * nothing to redo.
	 *
	 * VBAR_EL1 is written here on ARM, to point at the exception vectors.
	 * On this architecture it is written twice and much earlier - with
	 * the table's physical address before the MMU comes on, and with its
	 * virtual address right after the move - because a fault in between
	 * has to be diagnosable. That lands with the exception path.
	 */
	pg_clear();
	pg_load();

	prot_init_done = 1;
}

/*===========================================================================*
 *				arch_post_init				     *
 *===========================================================================*/
void
arch_post_init(void)
{
	struct proc *vm;

	/*
	 * Tell the memory code which address space is loaded. VM is the boot
	 * process and it runs in the bootstrap page table, so the two are the
	 * same thing until VM has built one of its own.
	 */
	vm = proc_addr(VM_PROC_NR);
	get_cpulocal_var(ptproc) = vm;
	pg_info(&vm->p_seg.p_ttbr, &vm->p_seg.p_ttbr_v);
}

/*===========================================================================*
 *				bootmod					     *
 *===========================================================================*/
/* The boot module that holds a given process's image. */
static multiboot_module_t *
bootmod(int pnr)
{
	int i;

	assert(pnr >= 0);

	/*
	 * The first NR_TASKS entries of the boot image are the kernel's own
	 * tasks and have no module behind them, so the search starts past
	 * them.
	 */
	for (i = NR_TASKS; i < NR_BOOT_PROCS; i++) {
		int p = i - NR_TASKS;

		if (image[i].proc_nr == pnr) {
			assert(p < MULTIBOOT_MAX_MODS);
			assert(p < kinfo.mbi.mi_mods_count);
			return &kinfo.module_list[p];
		}
	}

	panic("boot module %d not found", pnr);
}

/*===========================================================================*
 *				libexec_pg_alloc			     *
 *===========================================================================*/
static int
alloc_for_vm;

static int
libexec_pg_alloc(struct exec_info *execi, vir_bytes vaddr, size_t len)
{
	pg_map(PG_ALLOCATEME, vaddr, vaddr + len, &kinfo);
	pg_load();
	memset((char *)vaddr, 0, len);
	alloc_for_vm += len;

	return OK;
}

/*===========================================================================*
 *				arch_boot_proc				     *
 *===========================================================================*/
void
arch_boot_proc(struct boot_image *ip, struct proc *rp)
{
	multiboot_module_t *mod;
	struct ps_strings *psp;
	vir_bytes sp;

	if (rp->p_nr < 0)
		return;

	mod = bootmod(rp->p_nr);

	/*
	 * Only VM is loaded here, and only because it is the one process that
	 * cannot be loaded by VM. It goes into the bootstrap page table so
	 * that it can run and build everyone else's.
	 */
	if (rp->p_nr != VM_PROC_NR)
		return;

	{
		struct exec_info execi;

		memset(&execi, 0, sizeof(execi));

		execi.stack_high = kinfo.user_sp;
		execi.stack_size = 64 * 1024;	/* must be preallocated */
		execi.proc_e = ip->endpoint;
		/*
		 * The image is read where the loader left it, through the
		 * linear map. mod_start is physical, and a physical pointer
		 * is not usable here: pre_init() dropped the identity map
		 * before calling kmain(), and TTBR0 now holds the page table
		 * this function is loading VM into. phys2vir() is the upper
		 * half, which covers all of RAM and is never switched.
		 */
		execi.hdr = (char *)phys2vir(mod->mod_start);
		execi.filesize = execi.hdr_len = mod->mod_end - mod->mod_start;
		strlcpy(execi.progname, ip->proc_name, sizeof(execi.progname));
		execi.frame_len = 0;

		execi.copymem = libexec_copy_memcpy;
		execi.clearmem = libexec_clear_memset;
		execi.allocmem_prealloc_junk = libexec_pg_alloc;
		execi.allocmem_prealloc_cleared = libexec_pg_alloc;
		execi.allocmem_ondemand = libexec_pg_alloc;
		execi.clearproc = NULL;

		if (libexec_load_elf(&execi) != OK)
			panic("VM loading failed");

		/*
		 * The initial stack: a ps_strings block, and below it the
		 * three words the C startup code reads as argc, argv and
		 * envp.
		 *
		 * The alignment is the part that is not inherited. AArch64
		 * requires SP to be 16-byte aligned whenever it is used as a
		 * base register, and an EL0 entry with a misaligned SP raises
		 * an SP alignment fault before the process executes an
		 * instruction - so this rounds down rather than trusting the
		 * arithmetic to come out even, which is what the 32-bit ports
		 * can afford to do. kinfo.user_sp is page-aligned, and
		 * sizeof(struct ps_strings) is 24 on LP64, so without this
		 * the first process would fault on entry with nothing to
		 * point at the cause.
		 */
		sp = execi.stack_high - sizeof(struct ps_strings);
		psp = (struct ps_strings *)sp;

		sp -= 2 * sizeof(void *) + sizeof(int);
		sp &= ~(vir_bytes)15;

		psp->ps_argvstr = (char **)(sp + sizeof(int));
		psp->ps_nargvstr = 0;
		psp->ps_envstr = psp->ps_argvstr + 1;
		psp->ps_nenvstr = 0;

		arch_proc_init(rp, execi.pc, sp,
		    execi.stack_high - sizeof(struct ps_strings),
		    ip->proc_name);

		/*
		 * The image has been copied into the pages VM will run from,
		 * so the module itself is free memory again.
		 */
		add_memmap(&kinfo, mod->mod_start,
		    mod->mod_end - mod->mod_start);
		mod->mod_end = mod->mod_start = 0;

		kinfo.vm_allocated_bytes = alloc_for_vm;
	}
}
