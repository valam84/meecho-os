# Makefile for arch-dependent kernel code, aarch64.
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}

# No SRCS yet, and no .PATH on purpose.
#
# What is in this directory today is the stand-alone bring-up kernel of
# stage 2 - head.S, mmu.c, trap.c, proc.c, intr.c, usercopy.S and the rest -
# built by Makefile.bringup with an external cross compiler, and it is not
# the architecture layer of the generic kernel. Some of its file names are
# also the generic kernel's own (proc.c), so putting this directory on .PATH
# would make it ambiguous which one a source refers to.
#
# The architecture layer is written by taking that code file by file and
# fitting it to the interfaces in kernel/proto.h; each file joins SRCS as it
# lands. Until then this file exists so that the tree parses and the
# architecture-independent kernel sources compile: the link then names
# exactly what the layer still owes.
#
# See port/PORTING-LOG.md, stage 4.

# The unpaged object machinery earm and i386 carry is deliberately absent.
# Both start with the MMU off, run pre_init and pg_utils against physical
# addresses, and therefore link a second copy of those objects under a
# __k_unpaged_ symbol prefix. This kernel does not: early code is
# PC-relative, it enables the MMU itself and moves to the upper half in one
# step, so every object exists once. See PORTING-LOG.md, stage 2.3.
