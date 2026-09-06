/*
 * The MMIO transport for libvirtio: a virtio device that is a "virtio,mmio"
 * node in the device tree.
 *
 * On a machine described by a device tree there is no bus to enumerate and
 * no PCI server to ask. The tree names each transport - QEMU's virt has
 * thirty-two of them, 512 bytes apart from 0x0a000000 - and each is a page
 * of registers that says what, if anything, is behind it: a magic number,
 * a layout version, and the virtio device ID (1 net, 2 block, 0 nothing).
 * Finding "the second block device" means walking the tree, mapping each
 * candidate, and reading those three words.
 *
 * Which of them a driver may map at all is decided before it runs: RS
 * grants the "reg" and "interrupts" of every node matching the "devicetree"
 * line of the driver's entry in system.conf, and the mapping below is
 * refused for anything else. So the walk here is the same walk RS did,
 * asked a narrower question.
 *
 * This speaks version 1 of the register layout, the legacy one, because that
 * is what the ring code in virtio.c assumes: 32-bit feature words, a queue
 * told where it is by page frame number, page-aligned ring layout. QEMU
 * offers version 1 by default (virtio-mmio.force-legacy=true). A version 2
 * transport is refused with a message rather than driven wrongly.
 *
 * Registers are reached with ordinary loads and stores through a mapping
 * that VM makes with device attributes, so accesses to them are not
 * reordered against each other. Against the rings, which are normal
 * memory, the barrier in virtio.c before every notify and after every
 * interrupt is what keeps the order.
 */

#define _SYSTEM 1

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>				/* MAP_FAILED */

#include <machine/vmparam.h>			/* PAGE_SIZE */

#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/vm.h>
#include <minix/fdt.h>

#include "virtio_impl.h"

/* Register offsets, version 1 of the layout. */
#define VIRTIO_MMIO_MAGIC_VALUE		0x000	/* "virt", little-endian */
#define VIRTIO_MMIO_VERSION		0x004
#define VIRTIO_MMIO_DEVICE_ID		0x008
#define VIRTIO_MMIO_VENDOR_ID		0x00c
#define VIRTIO_MMIO_HOST_FEATURES	0x010
#define VIRTIO_MMIO_HOST_FEATURES_SEL	0x014
#define VIRTIO_MMIO_GUEST_FEATURES	0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL	0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE	0x028
#define VIRTIO_MMIO_QUEUE_SEL		0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034
#define VIRTIO_MMIO_QUEUE_NUM		0x038
#define VIRTIO_MMIO_QUEUE_ALIGN		0x03c
#define VIRTIO_MMIO_QUEUE_PFN		0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064
#define VIRTIO_MMIO_STATUS		0x070
#define VIRTIO_MMIO_CONFIG		0x100

#define VIRTIO_MMIO_MAGIC		0x74726976
#define VIRTIO_MMIO_LEGACY_VERSION	1

#define VIRTIO_MMIO_COMPATIBLE		"virtio,mmio"

/* The register page is 512 bytes on QEMU; the tree says, and this is the
 * least we expect it to say. */
#define VIRTIO_MMIO_MIN_LEN		0x200

/* More transports than any machine in sight; virt has thirty-two. */
#define MAX_TRANSPORTS			64

struct transport {
	phys_bytes base;
	size_t len;
	int irq;
};

struct scan {
	struct transport t[MAX_TRANSPORTS];
	int n;
};

static u32_t
mmio_read32(struct virtio_device *dev, i32_t off)
{
	return *(volatile u32_t *)(dev->base + off);
}

static void
mmio_write32(struct virtio_device *dev, i32_t off, u32_t val)
{
	*(volatile u32_t *)(dev->base + off) = val;
}

/*
 * One "virtio,mmio" node: its first reg pair and its first interrupt. The
 * list is kept sorted by address, so that instance 0 is the lowest
 * transport whatever order the tree lists them in - QEMU writes them
 * highest first, and attaches the first -device to the lowest.
 */
static int
scan_node(void *cookie, int depth, const char *name,
	const struct fdt_node *node)
{
	struct scan *s = cookie;
	struct transport t;
	u64_t base, len;
	int i;

	if (depth == 0 || !fdt_node_is_compatible(node, VIRTIO_MMIO_COMPATIBLE))
		return 0;

	if (fdt_node_reg(node, 0, &base, &len) != 0)
		return 0;
	if (len < VIRTIO_MMIO_MIN_LEN)
		return 0;

	t.base = (phys_bytes)base;
	t.len = (size_t)len;
	t.irq = fdt_node_gic_irq(node, 0);

	if (s->n >= MAX_TRANSPORTS)
		return 0;

	for (i = s->n; i > 0 && s->t[i - 1].base > t.base; i--)
		s->t[i] = s->t[i - 1];
	s->t[i] = t;
	s->n++;

	return 0;
}

static int
map_transport(struct virtio_device *dev, const struct transport *t)
{
	void *v;

	if ((v = vm_map_phys(SELF, (void *)t->base, t->len)) == MAP_FAILED)
		return EPERM;

	dev->base = (vir_bytes)v;
	dev->base_len = t->len;
	dev->irq = t->irq;

	return OK;
}

static void
unmap_transport(struct virtio_device *dev)
{
	if (dev->base != 0)
		vm_unmap_phys(SELF, (void *)dev->base, dev->base_len);
	dev->base = 0;
	dev->base_len = 0;
}

int
virtio_transport_find(struct virtio_device *dev, u16_t devid, int skip)
{
	struct scan *s;
	void *dtb;
	int i, r, version_complained = 0;

	if ((dtb = fdt_fetch()) == NULL) {
		printf("%s: no device tree, so no way to find the device\n",
		    dev->name);
		return ENXIO;
	}

	if ((s = malloc(sizeof(*s))) == NULL) {
		free(dtb);
		return ENOMEM;
	}
	memset(s, 0, sizeof(*s));

	(void)fdt_walk(dtb, scan_node, s);
	free(dtb);

	r = ENXIO;
	for (i = 0; i < s->n; i++) {
		u32_t magic, version, id;

		/*
		 * A transport RS did not grant is not ours to look at; the
		 * mapping fails, and that is not an error, it is the answer.
		 */
		if (map_transport(dev, &s->t[i]) != OK)
			continue;

		magic = mmio_read32(dev, VIRTIO_MMIO_MAGIC_VALUE);
		version = mmio_read32(dev, VIRTIO_MMIO_VERSION);
		id = mmio_read32(dev, VIRTIO_MMIO_DEVICE_ID);

		if (magic != VIRTIO_MMIO_MAGIC || id != devid) {
			unmap_transport(dev);
			continue;
		}

		if (version != VIRTIO_MMIO_LEGACY_VERSION) {
			if (!version_complained) {
				printf("%s: virtio-mmio at 0x%lx is layout "
				    "version %u; only version 1 is driven "
				    "(QEMU: -global "
				    "virtio-mmio.force-legacy=true)\n",
				    dev->name, (unsigned long)s->t[i].base,
				    version);
				version_complained = 1;
			}
			unmap_transport(dev);
			continue;
		}

		if (skip > 0) {
			skip--;
			unmap_transport(dev);
			continue;
		}

		if (dev->irq < 0) {
			printf("%s: virtio-mmio at 0x%lx has no interrupt "
			    "this system can route\n", dev->name,
			    (unsigned long)s->t[i].base);
			unmap_transport(dev);
			r = EINVAL;
			break;
		}

		/* Ours. Reset it, and tell it the page size the rings use. */
		mmio_write32(dev, VIRTIO_MMIO_STATUS, 0);
		mmio_write32(dev, VIRTIO_MMIO_GUEST_PAGE_SIZE, PAGE_SIZE);
		r = OK;
		break;
	}

	free(s);
	return r;
}

void
virtio_transport_free(struct virtio_device *dev)
{
	unmap_transport(dev);
}

u32_t
virtio_transport_host_features(struct virtio_device *dev)
{
	/* The first 32 bits, which are all the legacy interface has. */
	mmio_write32(dev, VIRTIO_MMIO_HOST_FEATURES_SEL, 0);
	return mmio_read32(dev, VIRTIO_MMIO_HOST_FEATURES);
}

void
virtio_transport_guest_features(struct virtio_device *dev, u32_t f)
{
	mmio_write32(dev, VIRTIO_MMIO_GUEST_FEATURES_SEL, 0);
	mmio_write32(dev, VIRTIO_MMIO_GUEST_FEATURES, f);
}

void
virtio_transport_queue_select(struct virtio_device *dev, u16_t q)
{
	mmio_write32(dev, VIRTIO_MMIO_QUEUE_SEL, q);
}

u16_t
virtio_transport_queue_size(struct virtio_device *dev)
{
	return (u16_t)mmio_read32(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
}

void
virtio_transport_queue_set(struct virtio_device *dev, u16_t num, u32_t pfn)
{
	/*
	 * Unlike PCI, the driver states the size it will use and the
	 * alignment the ring's used part is at, then the page frame.
	 */
	mmio_write32(dev, VIRTIO_MMIO_QUEUE_NUM, num);
	mmio_write32(dev, VIRTIO_MMIO_QUEUE_ALIGN, PAGE_SIZE);
	mmio_write32(dev, VIRTIO_MMIO_QUEUE_PFN, pfn);
}

void
virtio_transport_queue_notify(struct virtio_device *dev, u16_t q)
{
	mmio_write32(dev, VIRTIO_MMIO_QUEUE_NOTIFY, q);
}

void
virtio_transport_set_status(struct virtio_device *dev, u8_t status)
{
	mmio_write32(dev, VIRTIO_MMIO_STATUS, status);
}

u8_t
virtio_transport_isr(struct virtio_device *dev)
{
	u32_t status;

	/* Unlike PCI, reading does not acknowledge; writing back does. */
	status = mmio_read32(dev, VIRTIO_MMIO_INTERRUPT_STATUS);
	if (status != 0)
		mmio_write32(dev, VIRTIO_MMIO_INTERRUPT_ACK, status);

	return (u8_t)status;
}

/*
 * The configuration space is byte-addressable in the legacy layout, and
 * the device-specific structures are laid out in the guest's own byte
 * order, so a field is read at its natural width.
 */
u32_t
virtio_transport_config_read32(struct virtio_device *dev, i32_t off)
{
	return *(volatile u32_t *)(dev->base + VIRTIO_MMIO_CONFIG + off);
}

u16_t
virtio_transport_config_read16(struct virtio_device *dev, i32_t off)
{
	return *(volatile u16_t *)(dev->base + VIRTIO_MMIO_CONFIG + off);
}

u8_t
virtio_transport_config_read8(struct virtio_device *dev, i32_t off)
{
	return *(volatile u8_t *)(dev->base + VIRTIO_MMIO_CONFIG + off);
}

void
virtio_transport_config_write32(struct virtio_device *dev, i32_t off,
	u32_t val)
{
	*(volatile u32_t *)(dev->base + VIRTIO_MMIO_CONFIG + off) = val;
}

void
virtio_transport_config_write16(struct virtio_device *dev, i32_t off,
	u16_t val)
{
	*(volatile u16_t *)(dev->base + VIRTIO_MMIO_CONFIG + off) = val;
}

void
virtio_transport_config_write8(struct virtio_device *dev, i32_t off,
	u8_t val)
{
	*(volatile u8_t *)(dev->base + VIRTIO_MMIO_CONFIG + off) = val;
}
