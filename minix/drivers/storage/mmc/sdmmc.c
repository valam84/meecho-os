/*
 * The block device: minors, partitions, grants, and the bounce buffer.
 *
 * Two things here are worth reading before changing anything.
 *
 * The bounce buffer is not a workaround. What a grant names is somewhere in
 * the caller's address space, in pages this driver has no physical address
 * for and which need not be contiguous; the controller fetches by physical
 * address and walks straight through. So the transfer happens into memory
 * this driver owns, and the grant copy happens around it.
 *
 * That buffer is obtained from alloc_contig() rather than declared, and both
 * of its properties matter. Contiguous, because one command has to be one
 * range - a bounce buffer split across pages would be a descriptor list
 * assembled on every request, for no gain. And with its physical address
 * known, because that is the only form the controller understands; the
 * address is handed down to the card layer and on to the host, where a host
 * that has DMA uses it and one that has not ignores it. See
 * port/PORTING-LOG.md, "Этап 9", for the cache maintenance that makes the
 * two views of this buffer agree.
 *
 * The vector libblockdriver hands over is an iovec_t whose iov_addr is a
 * grant for anyone else's request and a plain pointer for the driver's own
 * - which is how the partition table gets read. Those two are the same
 * width only on a 32-bit port. Reading a pointer through the iovec_s_t
 * view keeps its low half, sign-extended, and every partition table read
 * fails with EFAULT; the mmc driver next door still has that bug, and
 * virtio_blk and ahci have had it fixed. Hence copy_to_caller() and
 * copy_from_caller() below, which look at the endpoint first.
 */

#include <minix/blockdriver.h>
#include <minix/drivers.h>
#include <minix/drvlib.h>
#include <minix/log.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>

#include <sys/ioc_disk.h>

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sdmmc.h"

struct log sdmmc_log = {
	.name = "sdmmc",
	.log_level = LEVEL_INFO,
	.log_func = default_log
};

/*
 * How much moves in one command. Sixty-four sectors is 32 KiB, which is one
 * CMD18 or CMD25 and one grant copy per chunk.
 *
 * The buffer is allocated once, at start-up, and never again: a file system
 * writing at the moment memory runs short is exactly when a driver must not
 * be asking for any. It is page-aligned and a whole number of pages long,
 * which is what keeps the cache maintenance simple - no line of it is shared
 * with anything else, so nothing here has to worry about the partial-line
 * rule that sys_cachectl(2) documents.
 */
#define SDMMC_CHUNK_SECTORS	64
#define SDMMC_CHUNK_BYTES	(SDMMC_CHUNK_SECTORS * SDMMC_SECTOR_SIZE)

static uint8_t *chunk;
static phys_bytes chunk_phys;

static struct sdmmc_host host;
static struct sdmmc_card card;

static int open_count;

/* One drive, its four partitions and their sub-partitions. */
static struct device part[DEV_PER_DRIVE];
static struct device subpart[SUB_PER_DRIVE];

static int sdmmc_open(devminor_t minor, int access);
static int sdmmc_close(devminor_t minor);
static ssize_t sdmmc_transfer(devminor_t minor, int write, u64_t position,
	endpoint_t endpt, iovec_t *iov, unsigned int nr_req, int flags);
static int sdmmc_ioctl(devminor_t minor, unsigned long request,
	endpoint_t endpt, cp_grant_id_t grant, endpoint_t user_endpt);
static struct device *sdmmc_part(devminor_t minor);
static void sdmmc_geometry(devminor_t minor, struct part_geom *entry);
static int sdmmc_flush(devminor_t minor);

static struct blockdriver sdmmc_dtab = {
	.bdr_type	= BLOCKDRIVER_TYPE_DISK,
	.bdr_open	= sdmmc_open,
	.bdr_close	= sdmmc_close,
	.bdr_transfer	= sdmmc_transfer,
	.bdr_ioctl	= sdmmc_ioctl,
	.bdr_part	= sdmmc_part,
	.bdr_geometry	= sdmmc_geometry,
	.bdr_flush	= sdmmc_flush
	/*
	 * No bdr_discard. An eMMC can erase and trim, but the block protocol
	 * wants ENOSYS from a driver that has not implemented it rather than
	 * an OK that means nothing, and libblockdriver answers ENOSYS by
	 * itself when the entry is absent. Nothing in the file system asks
	 * for discard yet; when something does, this is where it goes.
	 */
};

static struct device *
sdmmc_part(devminor_t minor)
{
	if (minor >= 0 && minor < DEV_PER_DRIVE)
		return &part[minor];

	if (minor >= MINOR_d0p0s0) {
		minor -= MINOR_d0p0s0;
		if (minor >= SUB_PER_DRIVE)
			return NULL;
		return &subpart[minor];
	}

	return NULL;
}

static int
sdmmc_open(devminor_t minor, int access)
{
	struct device *dev = sdmmc_part(minor);

	if (dev == NULL)
		return ENXIO;
	if (!card.present)
		return ENXIO;

	if (open_count == 0) {
		memset(part, 0, sizeof(part));
		memset(subpart, 0, sizeof(subpart));
		part[0].dv_size = card.sectors * SDMMC_SECTOR_SIZE;
		partition(&sdmmc_dtab, 0, P_PRIMARY, 0 /* ATAPI */);
	}

	open_count++;
	return OK;
}

static int
sdmmc_close(devminor_t minor)
{
	if (sdmmc_part(minor) == NULL)
		return ENXIO;

	if (open_count == 0) {
		log_warn(&sdmmc_log, "closed once too often\n");
		return EINVAL;
	}

	open_count--;

	/* Last one out empties the card's cache. */
	if (open_count == 0)
		(void)sdmmc_card_flush();

	return OK;
}

/*
 * The two halves of a grant copy, each of which has to know that the driver
 * addresses its own memory by pointer and everyone else's by grant.
 */
static int
copy_to_caller(endpoint_t endpt, const iovec_t *iov, vir_bytes offset,
	const void *src, size_t bytes)
{
	if (endpt == SELF) {
		memcpy((char *)iov->iov_addr + offset, src, bytes);
		return OK;
	}

	return sys_safecopyto(endpt, (cp_grant_id_t)iov->iov_addr, offset,
	    (vir_bytes)src, bytes);
}

static int
copy_from_caller(endpoint_t endpt, const iovec_t *iov, vir_bytes offset,
	void *dst, size_t bytes)
{
	if (endpt == SELF) {
		memcpy(dst, (const char *)iov->iov_addr + offset, bytes);
		return OK;
	}

	return sys_safecopyfrom(endpt, (cp_grant_id_t)iov->iov_addr, offset,
	    (vir_bytes)dst, bytes);
}

static ssize_t
sdmmc_transfer(devminor_t minor, int write, u64_t position, endpoint_t endpt,
	iovec_t *iov, unsigned int nr_req, int flags)
{
	struct device *dev;
	u64_t end, pos;
	vir_bytes done, iov_done;
	unsigned int i;
	int r;

	if (!card.present)
		return ENXIO;
	if ((dev = sdmmc_part(minor)) == NULL)
		return ENXIO;
	if (nr_req > NR_IOREQS)
		return EINVAL;

	if (position % SDMMC_SECTOR_SIZE != 0) {
		log_warn(&sdmmc_log, "unaligned position %llu\n",
		    (unsigned long long)position);
		return EINVAL;
	}

	for (i = 0; i < nr_req; i++)
		if (iov[i].iov_size == 0 ||
		    iov[i].iov_size % SDMMC_SECTOR_SIZE != 0)
			return EINVAL;

	end = dev->dv_base + dev->dv_size;
	pos = dev->dv_base + position;
	if (pos >= end)
		return 0;

	done = 0;
	for (i = 0; i < nr_req; i++) {
		iov_done = 0;

		while (iov_done < iov[i].iov_size) {
			size_t bytes;
			uint32_t count;

			if (pos >= end)
				return done;

			bytes = iov[i].iov_size - iov_done;
			if (bytes > SDMMC_CHUNK_BYTES)
				bytes = SDMMC_CHUNK_BYTES;
			if (pos + bytes > end)
				bytes = (size_t)(end - pos);
			bytes -= bytes % SDMMC_SECTOR_SIZE;
			if (bytes == 0)
				return done;

			count = (uint32_t)(bytes / SDMMC_SECTOR_SIZE);

			if (write) {
				r = copy_from_caller(endpt, &iov[i], iov_done,
				    chunk, bytes);
				if (r != OK) {
					log_warn(&sdmmc_log, "grant read "
					    "failed: %d\n", r);
					return r;
				}
				r = sdmmc_card_write(pos / SDMMC_SECTOR_SIZE,
				    count, chunk, chunk_phys);
			} else {
				r = sdmmc_card_read(pos / SDMMC_SECTOR_SIZE,
				    count, chunk, chunk_phys);
			}

			if (r != OK) {
				log_warn(&sdmmc_log, "%s of %u sectors at "
				    "%llu failed: %d\n",
				    write ? "write" : "read", count,
				    (unsigned long long)
				    (pos / SDMMC_SECTOR_SIZE), r);
				return (done > 0) ? (ssize_t)done : EIO;
			}

			if (!write) {
				r = copy_to_caller(endpt, &iov[i], iov_done,
				    chunk, bytes);
				if (r != OK) {
					log_warn(&sdmmc_log, "grant write "
					    "failed: %d\n", r);
					return r;
				}
			}

			pos += bytes;
			iov_done += bytes;
			done += bytes;
		}
	}

	return done;
}

static int
sdmmc_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, endpoint_t UNUSED(user_endpt))
{
	if (sdmmc_part(minor) == NULL)
		return ENXIO;

	switch (request) {
	case DIOCOPENCT:
		return sys_safecopyto(endpt, grant, 0,
		    (vir_bytes)&open_count, sizeof(open_count));
	}

	/* DIOCFLUSH and DIOCDISCARD reach libblockdriver, not this. */
	return ENOTTY;
}

static int
sdmmc_flush(devminor_t minor)
{
	if (sdmmc_part(minor) == NULL)
		return ENXIO;

	return sdmmc_card_flush();
}

/*
 * A geometry, because some callers insist on one. The card has no heads and
 * no cylinders; this is the conventional lie every SD driver tells, and the
 * partition code only uses it to place a partition table that we did not
 * write.
 */
static void
sdmmc_geometry(devminor_t minor, struct part_geom *entry)
{
	if (minor != 0)
		return;

	entry->heads = 64;
	entry->sectors = 32;
	entry->cylinders = (unsigned)(card.sectors / (64 * 32));
}

static int
sef_cb_init_fresh(int type, sef_init_info_t *UNUSED(info))
{
	long v;
	int r;

	v = LEVEL_INFO;
	if (env_parse("log_level", "d", 0, &v, LEVEL_NONE,
	    LEVEL_TRACE) == EP_SET)
		sdmmc_log.log_level = (int)v;

	/*
	 * The transfer buffer, before anything is asked of the hardware: a
	 * driver that cannot get it cannot serve a single request, and
	 * finding that out at the first read would mean finding it out with
	 * a file system already mounted on top.
	 */
	chunk = alloc_contig(SDMMC_CHUNK_BYTES, AC_ALIGN4K, &chunk_phys);
	if (chunk == NULL) {
		log_warn(&sdmmc_log, "no %u bytes of contiguous memory for the "
		    "transfer buffer\n", (unsigned)SDMMC_CHUNK_BYTES);
		return ENOMEM;
	}
	log_debug(&sdmmc_log, "transfer buffer %u bytes at phys 0x%lx\n",
	    (unsigned)SDMMC_CHUNK_BYTES, (unsigned long)chunk_phys);

	if ((r = sdmmc_host_find(&host)) != OK) {
		log_warn(&sdmmc_log, "no SD/MMC controller this driver knows "
		    "is in the device tree\n");
		return r;
	}

	if ((r = host.init()) != OK) {
		log_warn(&sdmmc_log, "%s would not start: %d\n", host.name, r);
		return r;
	}

	if ((r = sdmmc_card_init(&host, &card)) != OK) {
		log_warn(&sdmmc_log, "no usable card: %d\n", r);
		host.exit();
		return r;
	}

	blockdriver_announce(type);
	return OK;
}

static void
sef_cb_signal_handler(int signo)
{
	if (signo != SIGTERM)
		return;

	if (card.present)
		(void)sdmmc_card_flush();
	if (open_count == 0) {
		host.exit();
		exit(0);
	}
}

static void
sef_local_startup(void)
{
	sef_setcb_init_fresh(sef_cb_init_fresh);
	sef_setcb_signal_handler(sef_cb_signal_handler);
	sef_startup();
}

int
main(int argc, char **argv)
{
	env_setargs(argc, argv);
	sef_local_startup();

	blockdriver_task(&sdmmc_dtab);

	return OK;
}
