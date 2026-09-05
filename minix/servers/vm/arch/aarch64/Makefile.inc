.include <bsd.own.mk>

#Arch-specific sources
.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}

# The pagetable.h that belongs beside this file is not here yet, and VM will
# not compile without it. It is not a renaming layer over <machine/vm.h> the
# way the other two are: those describe two levels of translation, a page
# directory and a page table, and VM's pt.c is written to that shape. AArch64
# translates through four levels, so the header cannot be written until pt.c
# knows about more than two - which is the VM half of stage 4.
#
# This file exists so that the tree parses and every other directory can be
# built. See port/PORTING-LOG.md, stage 4.
