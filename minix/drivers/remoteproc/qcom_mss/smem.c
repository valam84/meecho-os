/*
 * Qualcomm SMEM (smem.h).  The layout, from Linux's smem.c:
 *
 *   - at the start, the header: four proc_comm entries (64 bytes), 32
 *     version words (version[7] >> 16 is the heap's: 11 or 12), then
 *     initialized, free_offset, available, reserved, and a table of 512
 *     entries {allocated, offset, size, aux_base} - the global heap of
 *     version 11;
 *   - in the last 4 KiB, the partition table "$TOC": version 1, a count,
 *     and 48-byte entries {offset, size, flags, host0, host1 (16 bits
 *     each), cacheline}; after the entries an "SIII" info block whose
 *     num_items is the highest item number;
 *   - a partition starts with "$PRT", host0, host1, size, the free offsets
 *     of its uncached part (growing up from the 32-byte header) and its
 *     cached part (growing down from the end); an uncached item is a 16-byte
 *     header {0xa5a5, item, size, padding_data, padding_hdr} and its data;
 *   - version 12 keeps its global heap as the partition whose hosts are
 *     both 0xfffe;
 *   - allocation is under hardware lock 3 of the TCSR mutex: write the
 *     applications processor's id (1), read it back; 0 releases.
 */

#include "smem.h"

#include <string.h>

#define HDR_VERSION(i)		(64 + 4 * (i))
#define HDR_FREE		(64 + 128 + 4)
#define HDR_AVAIL		(64 + 128 + 8)
#define HDR_TOC			(64 + 128 + 16)
#define TOC_ENTRY		16

#define SBL_VERSION_INDEX	7
#define GLOBAL_HEAP_VERSION	11
#define GLOBAL_PART_VERSION	12
#define ITEM_LAST_FIXED		8
#define ITEM_COUNT		512
#define GLOBAL_HOST		0xfffe

#define PTABLE_SIZE		4096
#define PTABLE_HDR		32
#define PTABLE_ENTRY		48

#define PART_HDR		32
#define P_SIZE			8
#define P_FREE_UNCACHED		12
#define P_FREE_CACHED		16

#define PRIV_HDR		16
#define CANARY			0xa5a5

#define LOCK_APPS		1
#define LOCK_TRIES		100000		/* a microsecond each */

static inline void
barrier(void)
{
	__sync_synchronize();
}

void
smem_read(const struct smem *s, size_t off, void *buf, size_t n)
{
	uint8_t *b = buf;
	uint32_t w;

	while (n > 0 && (off & 3) != 0) {
		*b++ = s->base[off++];
		n--;
	}
	while (n >= 4) {
		w = smem_rd32(s, off);
		memcpy(b, &w, 4);
		b += 4;
		off += 4;
		n -= 4;
	}
	while (n-- > 0)
		*b++ = s->base[off++];
}

void
smem_write(const struct smem *s, size_t off, const void *buf, size_t n)
{
	const uint8_t *b = buf;
	uint32_t w;

	while (n > 0 && (off & 3) != 0) {
		s->base[off++] = *b++;
		n--;
	}
	while (n >= 4) {
		memcpy(&w, b, 4);
		smem_wr32(s, off, w);
		b += 4;
		off += 4;
		n -= 4;
	}
	while (n-- > 0)
		s->base[off++] = *b++;
}

/* A partition's header, checked as Linux checks it: 0 if it is not one. */
static int
part_ok(struct smem *s, size_t off, size_t size, unsigned host0,
	unsigned host1)
{
	uint8_t magic[4];

	if (off == 0 || size < PART_HDR || off + size > s->size ||
	    off + size < off)
		return 0;
	smem_read(s, off, magic, 4);
	if (memcmp(magic, "$PRT", 4) != 0 ||
	    smem_rd16(s, off + 4) != host0 || smem_rd16(s, off + 6) != host1 ||
	    smem_rd32(s, off + P_SIZE) != size ||
	    smem_rd32(s, off + P_FREE_UNCACHED) > size)
		return 0;
	return 1;
}

int
smem_init(struct smem *s)
{
	size_t pt, e, off, size;
	uint8_t magic[4];
	unsigned i, n, h0, h1, remote;

	memset(s->part, 0, sizeof(s->part));
	memset(&s->global, 0, sizeof(s->global));
	s->why = NULL;
	if (s->size < 2 * PTABLE_SIZE) {
		s->why = "too small";
		return -1;
	}
	s->version = smem_rd32(s, HDR_VERSION(SBL_VERSION_INDEX)) >> 16;
	s->item_count = ITEM_COUNT;

	pt = s->size - PTABLE_SIZE;
	smem_read(s, pt, magic, 4);
	n = 0;
	if (memcmp(magic, "$TOC", 4) == 0) {
		if (smem_rd32(s, pt + 4) != 1) {
			s->why = "partition table of an unknown version";
			return -1;
		}
		n = smem_rd32(s, pt + 8);
		if (PTABLE_HDR + (size_t)n * PTABLE_ENTRY + 16 > PTABLE_SIZE) {
			s->why = "partition table too long";
			return -1;
		}
	}

	switch (s->version) {
	case GLOBAL_PART_VERSION:
		if (n == 0) {
			s->why = "version 12 without a partition table";
			return -1;
		}
		e = pt + PTABLE_HDR + (size_t)n * PTABLE_ENTRY;
		smem_read(s, e, magic, 4);
		if (memcmp(magic, "SIII", 4) == 0)
			s->item_count = smem_rd16(s, e + 16);
		break;
	case GLOBAL_HEAP_VERSION:
		break;
	default:
		s->why = "a heap version this does not know";
		return -1;
	}

	for (i = 0; i < n; i++) {
		e = pt + PTABLE_HDR + (size_t)i * PTABLE_ENTRY;
		off = smem_rd32(s, e + 0);
		size = smem_rd32(s, e + 4);
		h0 = smem_rd16(s, e + 12);
		h1 = smem_rd16(s, e + 14);
		if (off == 0 || size == 0)
			continue;
		if (h0 == GLOBAL_HOST && h1 == GLOBAL_HOST) {
			if (!part_ok(s, off, size, h0, h1)) {
				s->why = "bad global partition";
				return -1;
			}
			s->global.off = off;
			s->global.size = size;
			s->global.cacheline = smem_rd32(s, e + 16);
			continue;
		}
		if (h0 == SMEM_HOST_APPS)
			remote = h1;
		else if (h1 == SMEM_HOST_APPS)
			remote = h0;
		else
			continue;
		if (remote >= SMEM_HOST_COUNT || s->part[remote].off != 0 ||
		    !part_ok(s, off, size, h0, h1)) {
			s->why = "bad private partition";
			return -1;
		}
		s->part[remote].off = off;
		s->part[remote].size = size;
		s->part[remote].cacheline = smem_rd32(s, e + 16);
	}
	if (s->version == GLOBAL_PART_VERSION && s->global.off == 0) {
		s->why = "no global partition";
		return -1;
	}
	return 0;
}

static struct smem_part *
which(struct smem *s, unsigned host)
{
	if (host < SMEM_HOST_COUNT && s->part[host].off != 0)
		return &s->part[host];
	if (s->global.off != 0)
		return &s->global;
	return NULL;		/* the version 11 heap */
}

static size_t
get_private(struct smem *s, const struct smem_part *p, unsigned item,
	size_t *size)
{
	size_t e, end, cl, hsz, esz, pad;

	e = p->off + PART_HDR;
	end = p->off + smem_rd32(s, p->off + P_FREE_UNCACHED);
	while (e < end) {
		if (smem_rd16(s, e) != CANARY) {
			s->why = "bad canary";
			return 0;
		}
		esz = smem_rd32(s, e + 4);
		hsz = smem_rd16(s, e + 10);
		if (smem_rd16(s, e + 2) == item) {
			pad = smem_rd16(s, e + 8);
			if (esz > p->size || pad > esz ||
			    e + PRIV_HDR + hsz + esz > p->off + p->size)
				return 0;
			if (size != NULL)
				*size = esz - pad;
			return e + PRIV_HDR + hsz;
		}
		e += PRIV_HDR + hsz + esz;
	}

	/* The cached part: headers after their data, from the end down. */
	cl = p->cacheline ? p->cacheline : 1;
	hsz = (PRIV_HDR + cl - 1) / cl * cl;
	e = p->off + p->size - hsz;
	end = p->off + smem_rd32(s, p->off + P_FREE_CACHED);
	while (e > end && e > p->off) {
		if (smem_rd16(s, e) != CANARY) {
			s->why = "bad canary";
			return 0;
		}
		esz = smem_rd32(s, e + 4);
		if (esz > e - p->off)
			return 0;
		if (smem_rd16(s, e + 2) == item) {
			pad = smem_rd16(s, e + 8);
			if (pad > esz)
				return 0;
			if (size != NULL)
				*size = esz - pad;
			return e - esz;
		}
		e -= esz + hsz;
	}
	return 0;
}

size_t
smem_get(struct smem *s, unsigned host, unsigned item, size_t *size)
{
	struct smem_part *p;
	size_t t, off, esz;

	if (item >= s->item_count)
		return 0;
	if ((p = which(s, host)) != NULL)
		return get_private(s, p, item, size);

	t = HDR_TOC + (size_t)item * TOC_ENTRY;
	if (smem_rd32(s, t) == 0)
		return 0;
	if ((smem_rd32(s, t + 12) & ~3u) != 0)
		return 0;		/* another region: none here */
	off = smem_rd32(s, t + 4);
	esz = smem_rd32(s, t + 8);
	if (off == 0 || off + esz > s->size || off + esz < off)
		return 0;
	if (size != NULL)
		*size = esz;
	return off;
}

static int
lock(struct smem *s)
{
	unsigned t;

	for (t = 0; t < LOCK_TRIES; t++) {
		*s->lock = LOCK_APPS;
		if (*s->lock == LOCK_APPS)
			return 0;
		s->delay_us(1);
	}
	s->why = "the hardware lock stayed taken";
	return -1;
}

static void
unlock(struct smem *s)
{
	barrier();
	*s->lock = 0;
}

static int
alloc_private(struct smem *s, const struct smem_part *p, unsigned item,
	size_t size)
{
	size_t e, end, cached, asz, esz;

	e = p->off + PART_HDR;
	end = p->off + smem_rd32(s, p->off + P_FREE_UNCACHED);
	cached = p->off + smem_rd32(s, p->off + P_FREE_CACHED);
	if (end > p->off + p->size || cached > p->off + p->size) {
		s->why = "partition offsets outside it";
		return -1;
	}
	while (e < end) {
		if (smem_rd16(s, e) != CANARY) {
			s->why = "bad canary";
			return -1;
		}
		if (smem_rd16(s, e + 2) == item)
			return 1;
		e += PRIV_HDR + smem_rd16(s, e + 10) + smem_rd32(s, e + 4);
	}
	esz = (size + 7) & ~(size_t)7;
	asz = PRIV_HDR + esz;
	if (e + asz > cached) {
		s->why = "partition full";
		return -1;
	}
	smem_wr16(s, e, CANARY);
	smem_wr16(s, e + 2, (uint16_t)item);
	smem_wr32(s, e + 4, (uint32_t)esz);
	smem_wr16(s, e + 8, (uint16_t)(esz - size));
	smem_wr16(s, e + 10, 0);
	smem_wr32(s, e + 12, 0);
	/* The header before the free offset moves past it: a remote that
	 * reads without the lock sees a list that is whole. */
	barrier();
	smem_wr32(s, p->off + P_FREE_UNCACHED,
	    smem_rd32(s, p->off + P_FREE_UNCACHED) + (uint32_t)asz);
	return 0;
}

static int
alloc_global(struct smem *s, unsigned item, size_t size)
{
	size_t t = HDR_TOC + (size_t)item * TOC_ENTRY;
	uint32_t free_off, avail;

	if (smem_rd32(s, t) != 0)
		return 1;
	size = (size + 7) & ~(size_t)7;
	free_off = smem_rd32(s, HDR_FREE);
	avail = smem_rd32(s, HDR_AVAIL);
	if (size > avail || free_off + size > s->size) {
		s->why = "heap full";
		return -1;
	}
	smem_wr32(s, t + 4, free_off);
	smem_wr32(s, t + 8, (uint32_t)size);
	barrier();
	smem_wr32(s, t, 1);
	smem_wr32(s, HDR_FREE, free_off + (uint32_t)size);
	smem_wr32(s, HDR_AVAIL, avail - (uint32_t)size);
	return 0;
}

int
smem_alloc(struct smem *s, unsigned host, unsigned item, size_t size)
{
	struct smem_part *p;
	int r;

	if (item < ITEM_LAST_FIXED || item >= s->item_count) {
		s->why = "item number not one to allocate";
		return -1;
	}
	if (lock(s) != 0)
		return -1;
	if ((p = which(s, host)) != NULL)
		r = alloc_private(s, p, item, size);
	else
		r = alloc_global(s, item, size);
	unlock(s);
	return r;
}
