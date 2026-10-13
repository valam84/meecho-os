/*
 * <linux/sync_file.h>: what a sync file answers - a fence of the GPU
 * service (mali) given away as a descriptor: poll(2) says POLLIN once every
 * fence in it is over (failed or not), and these ioctls:
 *
 *   SYNC_IOC_MERGE      a new sync file of this one's fences and fd2's
 *   SYNC_IOC_FILE_INFO  its state: status 1 over, 0 not yet, below zero
 *                       failed (minus the error); how many fences
 *
 * A sync file comes from DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD with
 * DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE and goes back with
 * DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE and _IMPORT_SYNC_FILE - which is what Mesa
 * does for every fence it makes - or from a dma-buf
 * (DMA_BUF_IOCTL_EXPORT_SYNC_FILE).
 *
 * The names, layouts and meanings are Linux's interface; the text is
 * MEECHO's (Linux's header is GPL), the numbers this system's ioctl
 * encoding.  See <linux/dma-buf.h> beside it.
 */
#ifndef _GFXCOMPAT_LINUX_SYNC_FILE_H_
#define _GFXCOMPAT_LINUX_SYNC_FILE_H_

#include <sys/ioccom.h>
#include <stdint.h>

struct sync_merge_data {
	char name[32];
	int32_t fd2;			/* the other sync file */
	int32_t fence;			/* out: the new one */
	uint32_t flags;			/* 0 */
	uint32_t pad;
};

struct sync_fence_info {
	char obj_name[32];
	char driver_name[32];
	int32_t status;
	uint32_t flags;
	uint64_t timestamp_ns;
};

struct sync_file_info {
	char name[32];
	int32_t status;
	uint32_t flags;			/* 0 */
	uint32_t num_fences;		/* in: room; out: how many there are */
	uint32_t pad;
	uint64_t sync_fence_info;	/* struct sync_fence_info[num_fences] */
};

#define SYNC_IOC_MAGIC		'>'
#define SYNC_IOC_MERGE		_IOWR(SYNC_IOC_MAGIC, 3, struct sync_merge_data)
#define SYNC_IOC_FILE_INFO	_IOWR(SYNC_IOC_MAGIC, 4, struct sync_file_info)

#endif /* _GFXCOMPAT_LINUX_SYNC_FILE_H_ */
