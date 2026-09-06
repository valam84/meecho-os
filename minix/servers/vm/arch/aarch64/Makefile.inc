.include <bsd.own.mk>

#Arch-specific sources
.PATH: ${.CURDIR}/arch/${MACHINE_ARCH}

# There are none: the pagetable.h beside this file is the whole of the
# architecture layer, as it is on the other two ports. What it has to say is
# longer, because four levels of translation do not rename onto the two that
# pagetable.c works in - see the header itself, and port/PORTING-LOG.md,
# stage 5.3.
