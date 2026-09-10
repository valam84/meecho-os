/* The kernel calls that are implemented in this file:
 *   m_type:	SYS_SETMCONTEXT
 *   m_type:	SYS_GETMCONTEXT
 *
 * The parameters for SYS_SETMCONTEXT kernel call are:
 *   m_lsys_krn_sys_setmcontext.endpt	# proc endpoint doing call
 *   m_lsys_krn_sys_setmcontext.ctx_ptr	# pointer to mcontext structure
 *
 * The parameters for SYS_GETMCONTEXT kernel call are:
 *   m_lsys_krn_sys_getmcontext.endpt	# proc endpoint doing call
 *   m_lsys_krn_sys_getmcontext.ctx_ptr	# pointer to mcontext structure
 */

#include "kernel/system.h"
#include <string.h>
#include <assert.h>
#include <machine/mcontext.h>

#if USE_MCONTEXT 

#if defined(__aarch64__)
/*
 * Moving the FP/SIMD register file between a process and its mcontext_t.
 *
 * Not one memcpy, unlike i386 and unlike do_sigsend()/do_sigreturn(): struct
 * sigcontext is ours and carries a struct fpu_state outright, while
 * mcontext_t is NetBSD's and stays NetBSD's - it is the published shape of a
 * machine context, which a debugger or a core dump reader is entitled to
 * walk, so the kernel bends to it rather than the other way round. It differs
 * from the kernel's save area in two ways, and only one of them is a size:
 *
 *	- __fregset_t is 528 bytes and struct fpu_state is 520. __qregs is
 *	  __aligned(16), so the struct rounds up past its two trailing words;
 *	- FPCR comes first in __fregset_t and second in struct fpu_state.
 *
 * The second is the dangerous one, and it is why this is done by name. FPSR
 * holds the cumulative exception bits and FPCR the rounding mode and the trap
 * enables, so a blind block copy would hand setmcontext() a set of exception
 * flags to install as a control register: a context resumed by swapcontext(3)
 * would go on computing in a rounding mode nobody asked for, quietly and
 * plausibly. The register file itself does move as one block, whose size is
 * checked here rather than trusted.
 */
typedef int _mcontext_fregs_matches_fpu_state[
	(sizeof(((mcontext_t *)0)->__fregs.__qregs) ==
	 sizeof(((struct fpu_state *)0)->fpu_regs) &&
	 offsetof(__fregset_t, __fpcr) ==
	 sizeof(((mcontext_t *)0)->__fregs.__qregs)) ? 1 : -1];
#endif

/*===========================================================================*
 *			      do_getmcontext				     *
 *===========================================================================*/
int do_getmcontext(struct proc * caller, message * m_ptr)
{
/* Retrieve machine context of a process */

  register struct proc *rp;
  int proc_nr, r;
  mcontext_t mc;

  if (!isokendpt(m_ptr->m_lsys_krn_sys_getmcontext.endpt, &proc_nr))
	return(EINVAL);
  if (iskerneln(proc_nr)) return(EPERM);
  rp = proc_addr(proc_nr);

#if defined(__i386__)
  if (!proc_used_fpu(rp))
	return(OK);	/* No state to copy */
#endif

  /* Get the mcontext structure into our address space.  */
  if ((r = data_copy(m_ptr->m_lsys_krn_sys_getmcontext.endpt,
		m_ptr->m_lsys_krn_sys_getmcontext.ctx_ptr, KERNEL,
		(vir_bytes) &mc, (phys_bytes) sizeof(mcontext_t))) != OK)
	return(r);

  mc.mc_flags = 0;
#if defined(__i386__)
  /* Copy FPU state */
  if (proc_used_fpu(rp)) {
	/* make sure that the FPU context is saved into proc structure first */
	save_fpu(rp);
	mc.mc_flags = (rp->p_misc_flags & MF_FPU_INITIALIZED) ? _MC_FPU_SAVED : 0;
	assert(sizeof(mc.__fpregs.__fp_reg_set) == FPU_XFP_SIZE);
	memcpy(&(mc.__fpregs.__fp_reg_set), rp->p_seg.fpu_state, FPU_XFP_SIZE);
  } 
#endif

#if defined(__aarch64__)
  /* Copy FP/SIMD state; see the note at the top of this file. */
  if (proc_used_fpu(rp)) {
	struct fpu_state *fs;

	/* The live owner has the newest copy; make the save area current. */
	save_fpu(rp);
	fs = (struct fpu_state *) rp->p_seg.fpu_state;

	mc.mc_flags = _MC_FPU_SAVED;
	memcpy(&mc.__fregs.__qregs, fs->fpu_regs, sizeof(fs->fpu_regs));
	mc.__fregs.__fpcr = fs->fpu_fpcr;
	mc.__fregs.__fpsr = fs->fpu_fpsr;
  }
#endif


  /* Copy the mcontext structure to the user's address space. */
  if ((r = data_copy(KERNEL, (vir_bytes) &mc,
	m_ptr->m_lsys_krn_sys_getmcontext.endpt,
	m_ptr->m_lsys_krn_sys_getmcontext.ctx_ptr,
	(phys_bytes) sizeof(mcontext_t))) != OK)
	return(r);

  return(OK);
}


/*===========================================================================*
 *			      do_setmcontext				     *
 *===========================================================================*/
int do_setmcontext(struct proc * caller, message * m_ptr)
{
/* Set machine context of a process */

  register struct proc *rp;
  int proc_nr, r;
  mcontext_t mc;

  if (!isokendpt(m_ptr->m_lsys_krn_sys_setmcontext.endpt, &proc_nr)) return(EINVAL);
  rp = proc_addr(proc_nr);

  /* Get the mcontext structure into our address space.  */
  if ((r = data_copy(m_ptr->m_lsys_krn_sys_setmcontext.endpt,
		m_ptr->m_lsys_krn_sys_setmcontext.ctx_ptr, KERNEL,
		(vir_bytes) &mc, (phys_bytes) sizeof(mcontext_t))) != OK)
	return(r);

#if defined(__i386__)
  /* Copy FPU state */
  if (mc.mc_flags & _MC_FPU_SAVED) {
	rp->p_misc_flags |= MF_FPU_INITIALIZED;
	assert(sizeof(mc.__fpregs.__fp_reg_set) == FPU_XFP_SIZE);
	memcpy(rp->p_seg.fpu_state, &(mc.__fpregs.__fp_reg_set), FPU_XFP_SIZE);
  } else
	rp->p_misc_flags &= ~MF_FPU_INITIALIZED;
  /* force reloading FPU in either case */
  release_fpu(rp);
#endif

#if defined(__aarch64__)
  /* Copy FP/SIMD state; see the note at the top of this file. */
  if (mc.mc_flags & _MC_FPU_SAVED) {
	struct fpu_state *fs = (struct fpu_state *) rp->p_seg.fpu_state;

	rp->p_misc_flags |= MF_FPU_INITIALIZED;
	memcpy(fs->fpu_regs, &mc.__fregs.__qregs, sizeof(fs->fpu_regs));
	fs->fpu_fpcr = mc.__fregs.__fpcr;
	fs->fpu_fpsr = mc.__fregs.__fpsr;
  } else
	rp->p_misc_flags &= ~MF_FPU_INITIALIZED;
  /*
   * Force reloading in either case. While rp still owns the register file it
   * is the live registers, not this save area, that returning to user leaves
   * in place - the save area would be read back only on the next hand-over,
   * which for the process that just called setmcontext() may never come.
   */
  release_fpu(rp);
#endif

  return(OK);
}

#endif
