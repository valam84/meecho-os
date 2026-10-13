/*
 * <linux/dma-buf.h>: what a dma-buf descriptor answers - a buffer of the
 * GPU service (mali) given away as a descriptor (DRM's PRIME): mmap(2) of
 * it maps the buffer, lseek(2) to its end says its size, and these ioctls:
 *
 *   DMA_BUF_IOCTL_SYNC            a processor's access begins (START: waits
 *                                 for the GPU's jobs that use the buffer)
 *                                 or ends (END: what it wrote, to memory)
 *   DMA_BUF_IOCTL_EXPORT_SYNC_FILE  a sync file of those jobs
 *   DMA_BUF_IOCTL_IMPORT_SYNC_FILE  a fence the next jobs using the buffer,
 *                                 and waits for it, wait for
 *
 * The names, layouts and meanings are Linux's interface, so that software
 * written for it (wlroots, Mesa's Vulkan) builds unchanged; the text is
 * MEECHO's - Linux's own header is GPL - and the numbers are this system's
 * ioctl encoding (<sys/ioccom.h>).  Not a stand-in like the rest of
 * libgfxcompat: this is the system's interface, kept beside the other
 * Linux-named header the graphics stack includes (input-event-codes.h).
 */
#ifndef _GFXCOMPAT_LINUX_DMA_BUF_H_
#define _GFXCOMPAT_LINUX_DMA_BUF_H_

#include <sys/ioccom.h>
#include <stdint.h>

struct dma_buf_sync {
	uint64_t flags;
};

#define DMA_BUF_SYNC_READ	(1 << 0)
#define DMA_BUF_SYNC_WRITE	(2 << 0)
#define DMA_BUF_SYNC_RW		(DMA_BUF_SYNC_READ | DMA_BUF_SYNC_WRITE)
#define DMA_BUF_SYNC_START	(0 << 2)
#define DMA_BUF_SYNC_END	(1 << 2)
#define DMA_BUF_SYNC_VALID_FLAGS_MASK \
	(DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END)

#define DMA_BUF_NAME_LEN	32

struct dma_buf_export_sync_file {
	uint32_t flags;			/* DMA_BUF_SYNC_READ/WRITE */
	int32_t fd;			/* out: the sync file */
};

struct dma_buf_import_sync_file {
	uint32_t flags;			/* DMA_BUF_SYNC_READ/WRITE */
	int32_t fd;			/* the sync file */
};

#define DMA_BUF_BASE		'b'
#define DMA_BUF_IOCTL_SYNC	_IOW(DMA_BUF_BASE, 0, struct dma_buf_sync)
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE \
	_IOWR(DMA_BUF_BASE, 2, struct dma_buf_export_sync_file)
#define DMA_BUF_IOCTL_IMPORT_SYNC_FILE \
	_IOW(DMA_BUF_BASE, 3, struct dma_buf_import_sync_file)

#endif /* _GFXCOMPAT_LINUX_DMA_BUF_H_ */
