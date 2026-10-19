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
#include "sdmmc_ioc.h"

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

/*
 * The window, with gptpart=NAME (sdmmc_gpt.c): the one partition of the
 * card this driver serves, in bytes from the card's start.  Minor 0 is the
 * window and nothing outside it is ever read or written, whatever the
 * partition code above believes.  win_hi == 0: no window, the whole card.
 */
static u64_t win_lo, win_hi;

/*
 * The read-only windows, with gptroN=NAME[,NAME...]: up to eight more
 * partitions, the first four served as minors 4, 3, 2, 1 (/dev/c0d0p3, p2,
 * p1, p0) and the next four as the sub-partition minors 143, 142, 141, 140
 * (/dev/c0d0p3s3, p3s2, p3s1, p3s0) in the order named, and only ever
 * read - to take a copy of something the
 * machine's own system keeps there (the Mi Pad 4's vendor partition and
 * the GPU's zap shader in it, port/clover/hw-gpu.md, §14), or for rmtfs to
 * hand the modem its own storage - modemst1, modemst2, fsg, fsc - which
 * rmtfs reads whole and keeps the modem's writes to in memory, as
 * postmarketOS runs it (port/clover/hw-modem.md) - and, with those four
 * taken, for fwimport to copy the firmware off modem and vendor at the
 * first boot (port/clover/rc.clover).  A window whose slot the partition
 * table inside the main window uses is not served.  None is ever written,
 * whatever minor a write comes by.
 */
#define RO_MAX		8
#define RO_MINOR(i)	((i) < 4 ? 4 - (int)(i) : \
			    MINOR_d0p0s0 + SUB_PER_DRIVE - 1 - ((int)(i) - 4))
static struct { u64_t lo, hi; } ro[RO_MAX];
static unsigned ro_n;

/* Does [lo, hi) touch any read-only window? */
static int
ro_touches(u64_t lo, u64_t hi)
{
	unsigned i;

	for (i = 0; i < ro_n; i++)
		if (lo < ro[i].hi && hi > ro[i].lo)
			return 1;
	return 0;
}

/* Does it lie wholly within one? */
static int
ro_within(u64_t lo, u64_t hi)
{
	unsigned i;

	for (i = 0; i < ro_n; i++)
		if (lo >= ro[i].lo && hi <= ro[i].hi)
			return 1;
	return 0;
}

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
static void window_identify(const char *name, u64_t lo);

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
	unsigned i;

	if (dev == NULL)
		return ENXIO;
	if (!card.present)
		return ENXIO;

	if (open_count == 0) {
		memset(part, 0, sizeof(part));
		memset(subpart, 0, sizeof(subpart));
		if (win_hi != 0) {
			part[0].dv_base = win_lo;
			part[0].dv_size = win_hi - win_lo;
		} else
			part[0].dv_size = card.sectors * SDMMC_SECTOR_SIZE;
		partition(&sdmmc_dtab, 0, P_PRIMARY, 0 /* ATAPI */);
		for (i = 0; i < ro_n; i++) {
			struct device *d = sdmmc_part(RO_MINOR(i));

			if (d->dv_size != 0) {
				log_warn(&sdmmc_log, "minor %d of the window "
				    "is a partition of its own; read-only "
				    "window %u is not served\n", RO_MINOR(i),
				    i);
				continue;
			}
			d->dv_base = ro[i].lo;
			d->dv_size = ro[i].hi - ro[i].lo;
		}
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

			/*
			 * The window is checked here, at the last step before
			 * the card, and not only through part[]: a partition
			 * table inside the window is the user's, and nothing
			 * it says may reach past the window's edges.
			 */
			if (write && ro_touches(pos, pos + bytes)) {
				log_warn(&sdmmc_log, "refused: write of %u "
				    "sectors at %llu reaches the read-only "
				    "window\n", count, (unsigned long long)
				    (pos / SDMMC_SECTOR_SIZE));
				return (done > 0) ? (ssize_t)done : EROFS;
			}
			if (win_hi != 0 && (pos < win_lo ||
			    pos + bytes > win_hi) && (write ||
			    !ro_within(pos, pos + bytes))) {
				log_warn(&sdmmc_log, "refused: %s of %u sectors "
				    "at %llu is outside the gptpart window\n",
				    write ? "write" : "read", count,
				    (unsigned long long)
				    (pos / SDMMC_SECTOR_SIZE));
				return (done > 0) ? (ssize_t)done : EIO;
			}
			/*
			 * A controller that carries the machine's own boot
			 * loaders (host.protect) is written only through a
			 * window: without gptpart= it is read-only.
			 */
			if (write && host.protect && win_hi == 0) {
				log_warn(&sdmmc_log, "refused: write to %s "
				    "without gptpartN=; the device holds the "
				    "boot loaders\n", host.name);
				return (done > 0) ? (ssize_t)done : EROFS;
			}

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
	case SDMMC_IOC_LOG:
		return sdmmc_ring_ioctl(endpt, grant);
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

	sdmmc_ring_install(&sdmmc_log);

	v = LEVEL_INFO;
	if (env_parse("log_level", "d", 0, &v, LEVEL_NONE,
	    LEVEL_TRACE) == EP_SET)
		sdmmc_log.log_level = (int)v;

	/*
	 * A second instance says so in every line: two copies of this driver
	 * print to one console, and on the target board one of them is the
	 * root's disk and the other a card in a socket.
	 */
	v = 0;
	(void)env_parse("instance", "d", 0, &v, 0, 3);
	if (v != 0) {
		static char name[8] = "sdmmc";

		name[5] = (char)('0' + v);
		sdmmc_log.name = name;
	}

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

	/*
	 * gptpartN=NAME, N the instance: serve that GPT partition and nothing
	 * else.  Numbered because a boot parameter reaches every instance,
	 * and the SD socket's card has no "userdata".  A table that does not
	 * check out, or a name that is not there exactly once, is a refusal
	 * to serve - never the whole card instead.
	 */
	{
		char name[40], key[16], list[80];
		u64_t first, count;
		long inst = 0;

		(void)env_parse("instance", "d", 0, &inst, 0, 3);
		snprintf(key, sizeof(key), "gptpart%ld", inst);
		if (env_get_param(key, name, sizeof(name)) == OK) {
			if ((r = sdmmc_gpt_find(name, card.sectors, chunk,
			    SDMMC_CHUNK_BYTES, chunk_phys, &first,
			    &count)) != OK) {
				log_warn(&sdmmc_log, "%s=%s: no such "
				    "partition to serve; not starting\n",
				    key, name);
				host.exit();
				return r;
			}
			win_lo = first * SDMMC_SECTOR_SIZE;
			win_hi = (first + count) * SDMMC_SECTOR_SIZE;
			window_identify(name, win_lo);
		} else if (host.protect) {
			log_info(&sdmmc_log, "%s holds the boot loaders: "
			    "read-only without %s=\n", host.name, key);
			/* What there is to choose from: read only. */
			(void)sdmmc_gpt_find(NULL, card.sectors, chunk,
			    SDMMC_CHUNK_BYTES, chunk_phys, &first, &count);
		}

		/*
		 * gptroN=NAME[,NAME...]: more partitions, read only (ro[]).
		 * Unlike the window, a name that is not there costs only
		 * itself: the machine's root may be in the window, and a copy
		 * wanted from elsewhere is no reason not to boot.
		 */
		snprintf(key, sizeof(key), "gptro%ld", inst);
		if (env_get_param(key, list, sizeof(list)) == OK) {
			char *next = list, *nm = NULL;

			while ((nm = next) != NULL && ro_n < RO_MAX) {
				if ((next = strchr(nm, ',')) != NULL)
					*next++ = '\0';
				if (*nm == '\0')
					continue;
				if (sdmmc_gpt_find(nm, card.sectors, chunk,
				    SDMMC_CHUNK_BYTES, chunk_phys, &first,
				    &count) != OK)
					log_warn(&sdmmc_log, "%s: %s: no such "
					    "partition; no read-only window\n",
					    key, nm);
				else if (win_hi != 0 && first *
				    SDMMC_SECTOR_SIZE < win_hi && (first +
				    count) * SDMMC_SECTOR_SIZE > win_lo)
					log_warn(&sdmmc_log, "%s: %s overlaps "
					    "the window; no read-only window\n",
					    key, nm);
				else {
					ro[ro_n].lo = first * SDMMC_SECTOR_SIZE;
					ro[ro_n].hi = (first + count) *
					    SDMMC_SECTOR_SIZE;
					log_info(&sdmmc_log, "%s: %llu sectors "
					    "from %llu, read only, as minor %d\n",
					    nm, (unsigned long long)count,
					    (unsigned long long)first,
					    RO_MINOR(ro_n));
					window_identify(nm, ro[ro_n].lo);
					ro_n++;
				}
			}
			if (nm != NULL)
				log_warn(&sdmmc_log, "%s: more than %d names; "
				    "the rest are not served\n", key, RO_MAX);
		}
	}

	blockdriver_announce(type);
	return OK;
}

/*
 * What the window starts with, read and only read - one line, so that a
 * person who meant to write a root image there can see whether it got
 * there before anything tries to mount it.  On the Mi Pad 4 the first root
 * on userdata failed with fsck's "bad magic number" and nothing on the
 * screen could say whether the partition still held Android's f2fs.
 * Magics: MFS V4 "MFS4" at byte 1024, MFS V3 0x4d5a at 1024 + 24
 * (minix/fs/mfs/ondisk.h), f2fs 0xf2f52010 at 1024, ext4 0xef53 at 1080.
 */
static void
window_identify(const char *name, u64_t lo)
{
	const uint8_t *b = chunk;
	const char *what;
	unsigned i;

	if (SDMMC_CHUNK_BYTES < 4096 ||
	    sdmmc_card_read(lo / SDMMC_SECTOR_SIZE, 8, chunk,
	    chunk_phys) != OK) {
		log_warn(&sdmmc_log, "%s: cannot read its first sectors\n",
		    name);
		return;
	}
	for (i = 0; i < 4096 && b[i] == 0; i++)
		;
	if (memcmp(b + 1024, "MFS4", 4) == 0)
		what = "a MEECHO file system (MFS V4)";
	else if (b[1024 + 24] == 0x5a && b[1024 + 25] == 0x4d)
		what = "an MFS V3 file system";
	else if (b[1024] == 0x10 && b[1025] == 0x20 && b[1026] == 0xf5 &&
	    b[1027] == 0xf2)
		what = "f2fs - Android's; no root image was written here";
	else if (b[1080] == 0x53 && b[1081] == 0xef)
		what = "ext4";
	else if (i == 4096)
		what = "zeros";
	else
		what = "nothing this driver recognises";
	log_info(&sdmmc_log, "%s starts with %s (at 1024: %02x %02x %02x %02x "
	    "%02x %02x %02x %02x)\n", name, what, b[1024], b[1025], b[1026],
	    b[1027], b[1028], b[1029], b[1030], b[1031]);
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
