/*
 * The mali service's own request, beside Panfrost's (stage 11.7): the
 * frame buffer driver's scanout buffers as a GPU buffer, so that the
 * compositor's GPU draws its frames straight into what the display
 * controller shows - no copy, and nothing of them in a processor's cache.
 *
 * DRM_IOCTL_MEECHO_SCANOUT_IMPORT names a scanout buffer by where the
 * caller has it mapped (FBIOC_MAP gave it the mapping): addr and size, the
 * whole buffer.  The service finds the physical pages there (the kernel's
 * umap, remote: the caller must have touched them) and takes them only if
 * they are one of the buffers the frame buffer driver published in DS
 * (fb.scanout.*) - memory that is never freed while that driver runs; any
 * other memory is EPERM.  The answer is a handle of the caller's context,
 * like CREATE_BO's: its GPU address in offset, and PRIME_HANDLE_TO_FD
 * makes it a dma-buf.  mmap(2) of that dma-buf maps the driver's region
 * of the buffer (fb.scanout.vir.N in the driver's space); the service
 * cleans its view of it around jobs only once a processor has mapped it.
 *
 * The includer brings DRM_IOWR and DRM_COMMAND_BASE (drm.h).
 */
#ifndef MEECHO_DRM_H
#define MEECHO_DRM_H

#include <stdint.h>

#define DRM_MEECHO_SCANOUT_IMPORT	0x30	/* above Panfrost's */

struct drm_meecho_scanout_import {
	uint64_t addr;		/* in: the caller's mapping of the buffer */
	uint64_t size;		/* in: bytes, a whole number of pages */
	uint32_t handle;	/* out */
	uint32_t pad;
	uint64_t offset;	/* out: the GPU address */
};

#define DRM_IOCTL_MEECHO_SCANOUT_IMPORT \
	DRM_IOWR(DRM_COMMAND_BASE + DRM_MEECHO_SCANOUT_IMPORT, \
	    struct drm_meecho_scanout_import)

/* The keys the frame buffer driver publishes in DS (u32 values). */
#define MEECHO_DS_SCANOUT_N	"fb.scanout.n"		/* buffers */
#define MEECHO_DS_SCANOUT_SIZE	"fb.scanout.size"	/* bytes each */
#define MEECHO_DS_SCANOUT	"fb.scanout.%d"		/* physical address */
#define MEECHO_DS_SCANOUT_VIR	"fb.scanout.vir.%d"	/* in the driver's space */
#define MEECHO_DS_FB_LABEL	"fb"			/* whose space that is */

#endif
