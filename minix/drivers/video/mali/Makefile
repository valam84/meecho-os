# Makefile for the Mali GPU service (Bifrost, found in the device tree)
PROG=	mali
SRCS=	mali.c mali_find.c mali_rk.c mali_mmu.c mali_bo.c mali_sched.c \
	mali_drm.c mali_job.c mali_umap.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The interface is DRM's and Panfrost's, from the headers its clients use:
# drm.h of libdrm, and panfrost_drm.h (the kernel's, MIT) beside libdrm's
# reachover; both are installed in /usr/include/libdrm.
CPPFLAGS+=	-I${NETBSDSRCDIR}/external/mit/libdrm/dist/include/drm \
		-I${NETBSDSRCDIR}/external/mit/libdrm/lib/libdrm/drm

# Buffers and fences as descriptors: <linux/dma-buf.h> and
# <linux/sync_file.h>, MEECHO's own, installed in /usr/include/gfxcompat.
CPPFLAGS+=	-I${NETBSDSRCDIR}/minix/lib/libgfxcompat

DPADD+=	${LIBCHARDRIVER} ${LIBSYS} ${LIBTIMERS}
LDADD+=	-lchardriver -lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
