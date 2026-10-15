WARNS?=		5

# AArch64: the libraries here are the system services' libraries, and a
# service must not touch FP/SIMD registers - see share/mk/minix.service.mk.
# There is no arithmetic in them, only structures GCC would otherwise copy
# through q31. The two that exist for the Wayland stack are user libraries
# and are left as they are.
.if ${MACHINE_ARCH} == "aarch64" && \
    ${LIB:U} != "epoll-shim" && ${LIB:U} != "gfxcompat"
CFLAGS+=	-mgeneral-regs-only
.endif
