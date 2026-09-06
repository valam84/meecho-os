# Makefile for libvirtio
.include <bsd.own.mk>

CPPFLAGS+= -D_MINIX_SYSTEM

LIB=	virtio

SRCS=	virtio.c

# The transport is the architecture's: a PCI I/O port window on a PC, a
# "virtio,mmio" node of the device tree on AArch64. See virtio_impl.h.
.if ${MACHINE_ARCH} == "aarch64"
SRCS+=	virtio_mmio.c
.else
SRCS+=	virtio_pci.c
.endif

.include <bsd.lib.mk>
