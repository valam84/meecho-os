/* $NetBSD: signal.h,v 1.1 2014/08/10 05:47:38 matt Exp $ */

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas of 3am Software Foundry.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _AARCH64_SIGNAL_H_
#define _AARCH64_SIGNAL_H_

#ifdef __aarch64__

#ifndef _LOCORE
typedef int sig_atomic_t;
#endif

#if defined(__minix)
#include <sys/featuretest.h>

#if defined(_LIBMINC) || !defined(_STANDALONE)
#include <machine/fpu.h>
#endif

#if defined(_NETBSD_SOURCE) && !defined(_LOCORE)
/*
 * Information pushed on the stack when a signal is delivered. The kernel
 * fills it in do_sigsend() and restores from it in do_sigreturn(); the
 * handler gets a pointer to it as its third argument.
 *
 * NetBSD/aarch64 has no struct sigcontext at all - it went to siginfo and
 * ucontext before this port existed - so this is MINIX's own. The register
 * file is laid out exactly as struct stackframe_s in
 * <machine/stackframe.h>: x0..x30, then sp, pc and the status register, so
 * that the kernel can copy the whole block in either direction.
 */
struct sigcontext {
	int		sc_onstack;	/* sigstack state to restore */
	int		__sc_mask13;	/* signal mask to restore (old style) */

	__uint64_t	sc_x[31];	/* x0..x30 */
	__uint64_t	sc_sp;
	__uint64_t	sc_pc;
	__uint64_t	sc_spsr;

	sigset_t	sc_mask;	/* signal mask to restore (new style) */
#if defined(_LIBMINC) || !defined(_STANDALONE)
	struct fpu_state sc_fpu_state;	/* valid when MF_FPU_INITIALIZED */
#endif
#define SC_MAGIC	0xc0ffee3
	int		sc_magic;
	int		sc_flags;
	int		trap_style;
};

__BEGIN_DECLS
int sigreturn(struct sigcontext *_scp);
__END_DECLS
#endif /* _NETBSD_SOURCE && !_LOCORE */
#endif /* defined(__minix) */

#elif defined(__arm__)

#include <arm/signal.h>

#endif /* __aarch64__/__arm__ */

#endif /* _AARCH64_SIGNAL_H_ */
