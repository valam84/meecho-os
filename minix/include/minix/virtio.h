/*
 * Generic virtio library for MINIX 3
 *
 * Copyright (c) 2013, A. Welzel, <arne.welzel@gmail.com>
 *
 * This software is released under the BSD license. See the LICENSE file
 * included in the main directory of this source distribution for the
 * license terms and conditions.
 */

#ifndef _MINIX_VIRTIO_H
#define _MINIX_VIRTIO_H 1

#include <sys/types.h>

#define VIRTIO_VENDOR_ID			0x1AF4

#define VIRTIO_STATUS_ACK			0x01
#define VIRTIO_STATUS_DRV			0x02
#define VIRTIO_STATUS_DRV_OK			0x04
#define VIRTIO_STATUS_FAIL			0x80

/*
 * Where the registers are and how they are reached is the transport's
 * business - PCI I/O ports on a PC, a "virtio,mmio" node of the device
 * tree on AArch64 - and the transport is chosen when the library is built.
 * A driver sees neither: it names the kind of device it drives and gets an
 * opaque handle; the only registers it reads through that handle are the
 * device-specific configuration space, by virtio_sread*() below.
 */

/* Feature description */
struct virtio_feature {
	const char *name;
	u8_t bit;
	u8_t host_support;
	u8_t guest_support;
};

/* Forward declaration of struct virtio_device.
 *
 * This structure is opaque to the caller.
 */
struct virtio_device;

/* Find the skip'th virtio device of this kind - 1 net, 2 block, and so on
 * as the specification numbers them; on PCI that number is the subsystem
 * device ID. Returns a pointer to an opaque virtio_device instance.
 */
struct virtio_device *virtio_setup_device(u16_t subdevid,
		const char *name,
		struct virtio_feature *features,
		int feature_count,
		int threads, int skip);

/* Attempt to allocate queue_cnt memory for queues */
int virtio_alloc_queues(struct virtio_device *dev, int num_queues);

/* Register the IRQ policy and indicate to the host we are ready to go */
void virtio_device_ready(struct virtio_device *dev);

/* Unregister the IRQ and reset the device */
void virtio_reset_device(struct virtio_device *dev);

/* Free the memory used by all queues */
void virtio_free_queues(struct virtio_device *dev);

/* Free all memory allocated for the device (except the queue memory,
 * which has to be freed before with virtio_free_queues()).
 *
 * Don't touch the device afterwards! This is like free(dev).
 */
void virtio_free_device(struct virtio_device *dev);


/* Feature helpers */
int virtio_guest_supports(struct virtio_device *dev, int bit);
int virtio_host_supports(struct virtio_device *dev, int bit);

/*
 * Use num vumap_phys elements and chain these as vring_desc elements
 * into the vring.
 *
 * Kick the queue if needed.
 *
 * data is opaque and returned by virtio_from_queue() when the host
 * processed the descriptor chain.
 *
 * Note: The last bit of vp_addr is used to flag whether an iovec is
 *	 writable. This implies that only word aligned buffers can be
 *	 used.
 */
int virtio_to_queue(struct virtio_device *dev, int qidx,
			struct vumap_phys *bufs, size_t num, void *data);

/*
 * If the host used a chain of descriptors, return 0, set data as was given to
 * virtio_to_queue(), and if len is not NULL, set it to the resulting length.
 * If the host has not processed any element, return -1.
 */
int virtio_from_queue(struct virtio_device *dev, int qidx, void **data,
	size_t *len);

/* IRQ related functions */
void virtio_irq_enable(struct virtio_device *dev);
void virtio_irq_disable(struct virtio_device *dev);

/* Checks the ISR field of the device and returns true if
 * the interrupt was for this device.
 */
int virtio_had_irq(struct virtio_device *dev);

/*
 * The device-specific configuration space, at byte offsets from its start:
 * for a block device the capacity is at 0, for a net device the MAC.
 */
u32_t virtio_sread32(struct virtio_device *dev, i32_t offset);
u16_t virtio_sread16(struct virtio_device *dev, i32_t offset);
u8_t virtio_sread8(struct virtio_device *dev, i32_t offset);
void virtio_swrite32(struct virtio_device *dev, i32_t offset, u32_t val);
void virtio_swrite16(struct virtio_device *dev, i32_t offset, u16_t val);
void virtio_swrite8(struct virtio_device *dev, i32_t offset, u8_t val);

#endif /* _MINIX_VIRTIO_H */
