/*
 * The legacy PCI transport for libvirtio.
 *
 * Copyright (c) 2013, A. Welzel, <arne.welzel@gmail.com>
 *
 * This software is released under the BSD license. See the LICENSE file
 * included in the main directory of this source distribution for the
 * license terms and conditions.
 *
 * This is the transport libvirtio was written for, separated from the ring
 * code so that an MMIO transport could stand beside it: a virtio device on
 * PCI, found by vendor ID 0x1af4 and a subsystem device ID that equals the
 * virtio device ID, with its registers in the I/O port window of BAR 0 at
 * the offsets the 0.9.5 specification gives. Every access is a port
 * instruction made on the driver's behalf by the kernel, so each one is a
 * kernel call; that is the price of I/O ports, not of this library.
 */

#define _SYSTEM 1

#include <errno.h>

#include <machine/pci.h>			/* PCI_ILR, PCI_BAR... */

#include <minix/syslib.h>
#include <minix/sysutil.h>

#include "virtio_impl.h"

/* Register offsets from the I/O port base, legacy layout. */
#define VIRTIO_HOST_F_OFF			0x0000
#define VIRTIO_GUEST_F_OFF			0x0004
#define VIRTIO_QADDR_OFF			0x0008

#define VIRTIO_QSIZE_OFF			0x000C
#define VIRTIO_QSEL_OFF				0x000E
#define VIRTIO_QNOTFIY_OFF			0x0010

#define VIRTIO_DEV_STATUS_OFF			0x0012
#define VIRTIO_ISR_STATUS_OFF			0x0013
#define VIRTIO_DEV_SPECIFIC_OFF			0x0014
/* if msi is enabled, device specific headers shift by 4 */
#define VIRTIO_MSI_ADD_OFF			0x0004

/* Just some wrappers around sys_read */
#define PCI_READ_XX(xx, suff)						\
static u##xx##_t							\
pci_read##xx(struct virtio_device *dev, i32_t off)			\
{									\
	int r;								\
	u32_t ret;							\
	if ((r = sys_in##suff(dev->port + off, &ret)) != OK)		\
		panic("%s: Read failed %d %d r=%d", dev->name,		\
						    dev->port,		\
						    off,		\
						    r);			\
									\
	return ret;							\
}

PCI_READ_XX(32, l)
PCI_READ_XX(16, w)
PCI_READ_XX(8, b)

/* Just some wrappers around sys_write */
#define PCI_WRITE_XX(xx, suff)						\
static void								\
pci_write##xx(struct virtio_device *dev, i32_t off, u##xx##_t val)	\
{									\
	int r;								\
	if ((r = sys_out##suff(dev->port + off, val)) != OK)		\
		panic("%s: Write failed %d %d r=%d", dev->name,		\
						     dev->port,		\
						     off,		\
						     r);		\
}

PCI_WRITE_XX(32, l)
PCI_WRITE_XX(16, w)
PCI_WRITE_XX(8, b)

int
virtio_transport_find(struct virtio_device *dev, u16_t devid, int skip)
{
	int r, devind, iof;
	u16_t vid, did, sdid;
	u32_t base, size;

	pci_init();

	r = pci_first_dev(&devind, &vid, &did);

	while (r > 0) {
		sdid = pci_attr_r16(devind, PCI_SUBDID);
		if (vid == VIRTIO_VENDOR_ID && sdid == devid) {

			/* this is the device we are looking for */
			if (skip == 0)
				break;

			skip--;
		}

		r = pci_next_dev(&devind, &vid, &did);
	}

	/* pci_[first|next_dev()] return 0 if no device was found */
	if (r == 0 || skip > 0)
		return ENXIO;

	pci_reserve(devind);

	if ((r = pci_get_bar(devind, PCI_BAR, &base, &size, &iof)) != OK) {
		printf("%s: Could not get BAR (%d)", dev->name, r);
		return r;
	}

	if (!iof) {
		printf("%s: PCI not IO space?", dev->name);
		return EINVAL;
	}

	if (base & 0xFFFF0000) {
		printf("%s: IO port weird (%08x)", dev->name, base);
		return EINVAL;
	}

	/* store the I/O port */
	dev->port = base;

	/* Reset the device */
	pci_write8(dev, VIRTIO_DEV_STATUS_OFF, 0);

	/* Read IRQ line */
	dev->irq = pci_attr_r8(devind, PCI_ILR);

	return OK;
}

void
virtio_transport_free(struct virtio_device *dev)
{
	/* The port window needs no unmapping; the reservation stays. */
}

u32_t
virtio_transport_host_features(struct virtio_device *dev)
{
	return pci_read32(dev, VIRTIO_HOST_F_OFF);
}

void
virtio_transport_guest_features(struct virtio_device *dev, u32_t f)
{
	pci_write32(dev, VIRTIO_GUEST_F_OFF, f);
}

void
virtio_transport_queue_select(struct virtio_device *dev, u16_t q)
{
	pci_write16(dev, VIRTIO_QSEL_OFF, q);
}

u16_t
virtio_transport_queue_size(struct virtio_device *dev)
{
	return pci_read16(dev, VIRTIO_QSIZE_OFF);
}

void
virtio_transport_queue_set(struct virtio_device *dev, u16_t num, u32_t pfn)
{
	/* On PCI the size is the device's to state, not the driver's. */
	pci_write32(dev, VIRTIO_QADDR_OFF, pfn);
}

void
virtio_transport_queue_notify(struct virtio_device *dev, u16_t q)
{
	pci_write16(dev, VIRTIO_QNOTFIY_OFF, q);
}

void
virtio_transport_set_status(struct virtio_device *dev, u8_t status)
{
	pci_write8(dev, VIRTIO_DEV_STATUS_OFF, status);
}

u8_t
virtio_transport_isr(struct virtio_device *dev)
{
	/* Reading is what acknowledges it. */
	return pci_read8(dev, VIRTIO_ISR_STATUS_OFF);
}

/* Device-specific reads take the MSI offset into account. */
#define PCI_CONFIG_XX(xx)						\
u##xx##_t								\
virtio_transport_config_read##xx(struct virtio_device *dev, i32_t off)	\
{									\
	off += VIRTIO_DEV_SPECIFIC_OFF;					\
	if (dev->msi)							\
		off += VIRTIO_MSI_ADD_OFF;				\
	return pci_read##xx(dev, off);					\
}									\
									\
void									\
virtio_transport_config_write##xx(struct virtio_device *dev, i32_t off,	\
	u##xx##_t val)							\
{									\
	off += VIRTIO_DEV_SPECIFIC_OFF;					\
	if (dev->msi)							\
		off += VIRTIO_MSI_ADD_OFF;				\
	pci_write##xx(dev, off, val);					\
}

PCI_CONFIG_XX(32)
PCI_CONFIG_XX(16)
PCI_CONFIG_XX(8)
