/*	sys/ioc_mmap.h - mmap(2) of a character device, the driver's half.
 *
 * VM maps files, block devices and memory objects itself: it knows what
 * their pages are.  A character device's memory only its driver can
 * name - the frames of a display, the buffers of a GPU - and such memory
 * is the driver's own, lent to a client with vm_remap().  So when mmap(2)
 * is given a character device, VFS answers VM with ENODEV, and libc puts
 * the same question to the driver with this request on the same
 * descriptor: the driver maps what cm_offset names into the caller and
 * says where.  A driver that does not know the request answers ENOTTY,
 * and mmap(2) fails with ENODEV, as it always did.
 *
 * cm_offset is the driver's own word for what to map - for a DRM device,
 * the token its MMAP_BO gave - and cm_len, cm_prot and cm_flags are those
 * of the mmap(2) call, for the driver to check.  cm_addr is where the
 * caller wants it with MAP_FIXED, a hint otherwise.
 */

#ifndef _SYS_IOC_MMAP_H
#define _SYS_IOC_MMAP_H

#include <minix/ioctl.h>
#include <sys/types.h>

struct cdev_mmap {
	void	*cm_addr;	/* in: where, if MAP_FIXED */
	size_t	 cm_len;	/* in: how much */
	int	 cm_prot;	/* in: PROT_* */
	int	 cm_flags;	/* in: MAP_* */
	off_t	 cm_offset;	/* in: what, in the driver's terms */
	void	*cm_ret;	/* out: where it is now */
};

#define MIOCMMAP	_IOWR('M', 32, struct cdev_mmap)

#endif /* _SYS_IOC_MMAP_H */
