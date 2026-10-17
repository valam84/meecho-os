# libdrmdriver: what a GPU service's DRM render node has whatever the GPU
# (<minix/drmdriver.h>) - contexts, the calls every render node answers,
# sync objects, dma-bufs and sync files as descriptors, waits, jobs that
# wait for fences, buffers' memory.  mali (CB2) and adreno (Mi Pad 4) stand
# on it; port/clover/hw-gpu.md, §9 and step 7.
.include <bsd.own.mk>

CPPFLAGS+= -D_MINIX_SYSTEM

LIB=	drmdriver

SRCS=	drm_dev.c drm_sync.c drm_mem.c drm_umap.c

# The interface is DRM's, from the headers its clients use: drm.h of
# libdrm (the reachover's copy, as the services take it), and the
# descriptors' <linux/dma-buf.h> and <linux/sync_file.h>, MEECHO's own.
CPPFLAGS+=	-I${NETBSDSRCDIR}/external/mit/libdrm/dist/include/drm \
		-I${NETBSDSRCDIR}/external/mit/libdrm/lib/libdrm/drm \
		-I${NETBSDSRCDIR}/minix/lib/libgfxcompat

.include <bsd.lib.mk>
