/*
 * The inside of libvirtio: the device structure and the transport interface.
 *
 * A virtio device is two things. One is the part the specification calls the
 * device itself - virtqueues in guest memory, a feature negotiation, a status
 * byte, a device-specific configuration space - and that part is the same
 * whatever bus the device sits on. The other is the transport: how the
 * registers that steer all that are reached. On a PC that is a PCI I/O port
 * window, found by vendor and device ID through the PCI server. On an
 * AArch64 machine like QEMU's virt there is no PCI server; the device is a
 * "virtio,mmio" node in the device tree, and its registers are a page of
 * memory-mapped I/O.
 *
 * virtio.c holds the first part and never touches a register directly: it
 * calls the functions declared here, and exactly one transport file -
 * virtio_pci.c or virtio_mmio.c, chosen by the architecture in the Makefile
 * - defines them. The driver above sees neither: it asks for a device by
 * its virtio device ID and gets back the opaque handle it always did.
 *
 * Both transports in the tree speak the legacy interface (virtio 0.9.5 on
 * PCI, version 1 of the MMIO register layout), which is what libvirtio's
 * ring code assumes: 32-bit features, queue addresses given as a page
 * frame number, rings laid out with page alignment. A modern transport
 * (VIRTIO_F_VERSION_1) would be a change to this interface, not to a
 * driver.
 */

#ifndef _LIBVIRTIO_VIRTIO_IMPL_H
#define _LIBVIRTIO_VIRTIO_IMPL_H 1

#include <minix/virtio.h>

#include "virtio_ring.h"

struct indirect_desc_table {
	int in_use;
	struct vring_desc *descs;
	phys_bytes paddr;
	size_t len;
};

struct virtio_queue {

	void *vaddr;				/* virtual addr of ring */
	phys_bytes paddr;			/* physical addr of ring */
	u32_t page;				/* physical guest page */

	u16_t num;				/* number of descriptors */
	u32_t ring_size;			/* size of ring in bytes */
	struct vring vring;

	u16_t free_num;				/* free descriptors */
	u16_t free_head;			/* next free descriptor */
	u16_t free_tail;			/* last free descriptor */
	u16_t last_used;			/* we checked in used */

	void **data;				/* points to pointers */
};

struct virtio_device {

	const char *name;			/* for debugging */

	/* Where the registers are; which of the two is used is the
	 * transport's business. */
	u16_t  port;				/* PCI: I/O port */
	vir_bytes base;				/* MMIO: mapped register base */
	size_t base_len;			/* MMIO: length of that mapping */

	u8_t status;				/* the status byte as written */

	struct virtio_feature *features;	/* host / guest features */
	u8_t num_features;			/* max 32 */

	struct virtio_queue *queues;		/* our queues */
	u16_t num_queues;

	int irq;				/* interrupt line */
	int irq_hook;				/* hook id */
	int msi;				/* is MSI enabled? */

	int threads;				/* max number of threads */

	struct indirect_desc_table *indirect;	/* indirect descriptor tables */
	int num_indirect;
};

/*
 * The rings live in memory the device reads and writes behind the CPU's
 * back, so the order in which the CPU's stores become visible matters: a
 * descriptor must be complete before the index that publishes it, and a
 * used index must be read before the element it covers. On x86 the
 * hardware keeps stores in order and a compiler barrier was enough; on
 * AArch64 it is not, and this has to be a real one.
 */
#define virtio_mb()	__sync_synchronize()

/*
 * The transport interface. Every function takes the device; the transport
 * fills in port or base, and irq, in virtio_transport_find().
 */

/*
 * Find the skip'th device with this virtio device ID (1 net, 2 block, ...),
 * make its registers reachable, reset it, and record its interrupt line.
 * OK, or ENXIO when there is no such device.
 */
int virtio_transport_find(struct virtio_device *dev, u16_t devid, int skip);

/* Undo what find() did to reach the registers. The device stays reset. */
void virtio_transport_free(struct virtio_device *dev);

u32_t virtio_transport_host_features(struct virtio_device *dev);
void virtio_transport_guest_features(struct virtio_device *dev, u32_t f);

/* Queue registers: select one, ask its maximum size, tell the device where
 * it is (its size and the guest page frame number of its ring). */
void virtio_transport_queue_select(struct virtio_device *dev, u16_t q);
u16_t virtio_transport_queue_size(struct virtio_device *dev);
void virtio_transport_queue_set(struct virtio_device *dev, u16_t num,
	u32_t pfn);
void virtio_transport_queue_notify(struct virtio_device *dev, u16_t q);

/* The status byte, whole. */
void virtio_transport_set_status(struct virtio_device *dev, u8_t status);

/* Read and acknowledge the interrupt status; non-zero if the device raised
 * one for a used ring or for a configuration change. */
u8_t virtio_transport_isr(struct virtio_device *dev);

/* The device-specific configuration space, at byte offsets from its start. */
u32_t virtio_transport_config_read32(struct virtio_device *dev, i32_t off);
u16_t virtio_transport_config_read16(struct virtio_device *dev, i32_t off);
u8_t virtio_transport_config_read8(struct virtio_device *dev, i32_t off);
void virtio_transport_config_write32(struct virtio_device *dev, i32_t off,
	u32_t val);
void virtio_transport_config_write16(struct virtio_device *dev, i32_t off,
	u16_t val);
void virtio_transport_config_write8(struct virtio_device *dev, i32_t off,
	u8_t val);

#endif /* _LIBVIRTIO_VIRTIO_IMPL_H */
