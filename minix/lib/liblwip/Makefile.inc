# AArch64: a service library, so no FP/SIMD - see minix/lib/Makefile.inc,
# which the libraries one level deeper than minix/lib/* do not reach.
.if ${MACHINE_ARCH} == "aarch64"
CFLAGS+=	-mgeneral-regs-only
.endif
