/* This file manages the super block table and the related data structures,
 * namely, the bit maps that keep track of which zones and which inodes are
 * allocated and which are free.  When a new inode or zone is needed, the
 * appropriate bit map is searched for a free entry.
 *
 * The entry points into this file are
 *   alloc_bit:       somebody wants to allocate a zone or inode; find one
 *   free_bit:        indicate that a zone or inode is available for allocation
 *   mounted:         tells if file inode is on mounted (or ROOT) file system
 *   read_super:      read a superblock
 */

#include "fs.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <minix/com.h>
#include <minix/u64.h>
#include <minix/bdev.h>
#include <machine/param.h>
#include <machine/vmparam.h>
#include "buf.h"
#include "inode.h"
#include "super.h"
#include "const.h"

/*===========================================================================*
 *				alloc_bit				     *
 *===========================================================================*/
bit_t alloc_bit(sp, map, origin)
struct super_block *sp;		/* the filesystem to allocate from */
int map;			/* IMAP (inode map) or ZMAP (zone map) */
bit_t origin;			/* number of bit to start searching at */
{
/* Allocate a bit from a bit map and return its bit number. */

  block_t start_block;		/* first bit block */
  block_t block;
  bit_t map_bits;		/* how many bits are there in the bit map? */
  short bit_blocks;		/* how many blocks are there in the bit map? */
  unsigned word, bcount;
  struct buf *bp;
  bitchunk_t *wptr, *wlim, k;
  bit_t i, b;

  if (sp->s_rd_only)
	panic("can't allocate bit on read-only filesys");

  if (map == IMAP) {
	start_block = START_BLOCK;
	map_bits = (bit_t) (sp->s_ninodes + 1);
	bit_blocks = sp->s_imap_blocks;
  } else {
	start_block = START_BLOCK + sp->s_imap_blocks;
	map_bits = (bit_t) (sp->s_zones - (sp->s_firstdatazone - 1));
	bit_blocks = sp->s_zmap_blocks;
  }

  /* Figure out where to start the bit search (depends on 'origin'). */
  if (origin >= map_bits) origin = 0;	/* for robustness */

  /* Locate the starting place. */
  block = (block_t) (origin / FS_BITS_PER_BLOCK(sp->s_block_size));
  word = (origin % FS_BITS_PER_BLOCK(sp->s_block_size)) / FS_BITCHUNK_BITS;

  /* Iterate over all blocks plus one, because we start in the middle. */
  bcount = bit_blocks + 1;
  do {
	bp = get_block(sp->s_dev, start_block + block, NORMAL);
	wlim = &b_bitmap(bp)[FS_BITMAP_CHUNKS(sp->s_block_size)];

	/* Iterate over the words in block. */
	for (wptr = &b_bitmap(bp)[word]; wptr < wlim; wptr++) {

		/* Does this word contain a free bit? */
		if (*wptr == (bitchunk_t) ~0) continue;

		/* Find and allocate the free bit. */
		k = (bitchunk_t) conv4(sp->s_native, (int) *wptr);
		for (i = 0; (k & (1 << i)) != 0; ++i) {}

		/* Bit number from the start of the bit map. */
		b = ((bit_t) block * FS_BITS_PER_BLOCK(sp->s_block_size))
		    + (wptr - &b_bitmap(bp)[0]) * FS_BITCHUNK_BITS
		    + i;

		/* Don't allocate bits beyond the end of the map. */
		if (b >= map_bits) break;

		/* Allocate and return bit number. */
		k |= 1 << i;
		*wptr = (bitchunk_t) conv4(sp->s_native, (int) k);
		MARKDIRTY(bp);
		put_block(bp);
		if(map == ZMAP) {
			used_zones++;
			lmfs_change_blockusage(1);
		}
		return(b);
	}
	put_block(bp);
	if (++block >= (unsigned int) bit_blocks) /* last block, wrap around */
		block = 0;
	word = 0;
  } while (--bcount > 0);
  return(NO_BIT);		/* no bit could be allocated */
}

/*===========================================================================*
 *				free_bit				     *
 *===========================================================================*/
void free_bit(sp, map, bit_returned)
struct super_block *sp;		/* the filesystem to operate on */
int map;			/* IMAP (inode map) or ZMAP (zone map) */
bit_t bit_returned;		/* number of bit to insert into the map */
{
/* Return a zone or inode by turning off its bitmap bit. */

  unsigned block, word, bit;
  struct buf *bp;
  bitchunk_t k, mask;
  block_t start_block;

  if (sp->s_rd_only)
	panic("can't free bit on read-only filesys");

  if (map == IMAP) {
	start_block = START_BLOCK;
  } else {
	start_block = START_BLOCK + sp->s_imap_blocks;
  }
  block = bit_returned / FS_BITS_PER_BLOCK(sp->s_block_size);
  word = (bit_returned % FS_BITS_PER_BLOCK(sp->s_block_size))
  	 / FS_BITCHUNK_BITS;

  bit = bit_returned % FS_BITCHUNK_BITS;
  mask = 1 << bit;

  bp = get_block(sp->s_dev, start_block + block, NORMAL);

  k = (bitchunk_t) conv4(sp->s_native, (int) b_bitmap(bp)[word]);
  if (!(k & mask)) {
  	if (map == IMAP) panic("tried to free unused inode");
  	else panic("tried to free unused block: %u", bit_returned);
  }

  k &= ~mask;
  b_bitmap(bp)[word] = (bitchunk_t) conv4(sp->s_native, (int) k);
  MARKDIRTY(bp);

  put_block(bp);

  if(map == ZMAP) {
	used_zones--;
	lmfs_change_blockusage(-1);
  }
}

/*===========================================================================*
 *				get_block_size				     *
 *===========================================================================*/
unsigned int get_block_size(dev_t dev)
{
  if (dev == NO_DEV)
  	panic("request for block size of NO_DEV");

  return(lmfs_fs_block_size());
}


/*===========================================================================*
 *				super_from_v3				     *
 *===========================================================================*/
static int super_from_v3(struct super_block *sp, const struct mfs3_super *d)
{
/* Fill in the in-core superblock from a V3 one on disk. */

  if (d->s_magic == MFS3_SUPER_MAGIC_V1 || d->s_magic == MFS3_SUPER_MAGIC_V2) {
	printf("MFS: only supports V3 and V4 filesystems.\n");
	return EINVAL;
  }
  if (d->s_magic != MFS3_SUPER_MAGIC)
	return EINVAL;

  memset(sp, 0, offsetof(struct super_block, s_inodes_per_block));

  sp->s_ninodes = d->s_ninodes;
  sp->s_zones = d->s_zones;
  sp->s_imap_blocks = (u32_t) d->s_imap_blocks;
  sp->s_zmap_blocks = (u32_t) d->s_zmap_blocks;
  sp->s_log_zone_size = (u32_t) d->s_log_zone_size;
  sp->s_flags = d->s_flags;
  sp->s_block_size = d->s_block_size;
  sp->s_inode_size = V2_INODE_SIZE;
  sp->s_version = V3;
  sp->s_native = 1;

  /*
   * Limit s_max_size to what the field can hold: it is an int32_t on disk,
   * not a host long.  The bound used to be spelled LONG_MAX, from a time
   * when the two agreed; under LP64 they do not.
   */
  if ((u32_t) d->s_max_size > INT32_MAX)
	sp->s_max_size = INT32_MAX;
  else
	sp->s_max_size = d->s_max_size;

  /* Zones consisting of multiple blocks are no longer supported. */
  if (sp->s_log_zone_size != 0) {
	printf("MFS: block and zone sizes are different\n");
	return EINVAL;
  }

  sp->s_inodes_per_block = V2_INODES_PER_BLOCK(sp->s_block_size);
  sp->s_ndzones = MFS3_NR_DZONES;
  sp->s_nindirs = V2_INDIRECTS(sp->s_block_size);
  sp->s_nlevels = MFS3_NR_LEVELS;
  sp->s_name_max = MFS3_DIRSIZ;

  /*
   * For a large disk the on-disk first data zone does not fit in its 16-bit
   * field, and a zero there means "compute it".
   */
  if (d->s_firstdatazone_old == 0) {
	block_t offset;

	offset = START_BLOCK + sp->s_imap_blocks + sp->s_zmap_blocks;
	offset += (sp->s_ninodes + sp->s_inodes_per_block - 1) /
		sp->s_inodes_per_block;

	sp->s_firstdatazone = offset;
  } else {
	sp->s_firstdatazone = (zone_t) d->s_firstdatazone_old;
  }

  return OK;
}

/*===========================================================================*
 *				super_to_v3				     *
 *===========================================================================*/
static void super_to_v3(const struct super_block *sp, struct mfs3_super *d)
{
/* The other way: only the fields a mounted V3 file system can change are
 * really at stake, but the whole block is rewritten, so all of them are
 * filled in.
 */
  memset(d, 0, sizeof(*d));

  d->s_ninodes = sp->s_ninodes;
  d->s_nzones = 0;			/* V2 field, unused since */
  d->s_imap_blocks = (int16_t) sp->s_imap_blocks;
  d->s_zmap_blocks = (int16_t) sp->s_zmap_blocks;
  d->s_firstdatazone_old =
	(sp->s_firstdatazone > UINT16_MAX) ? 0 : sp->s_firstdatazone;
  d->s_log_zone_size = (int16_t) sp->s_log_zone_size;
  d->s_flags = (uint16_t) sp->s_flags;
  d->s_max_size = (int32_t) sp->s_max_size;
  d->s_zones = sp->s_zones;
  d->s_magic = MFS3_SUPER_MAGIC;
  d->s_block_size = (uint16_t) sp->s_block_size;
  d->s_disk_version = 0;
}

/*===========================================================================*
 *				super_from_v4				     *
 *===========================================================================*/
static int super_from_v4(struct super_block *sp, const struct mfs4_super *d)
{
/* Fill in the in-core superblock from a V4 one on disk, and refuse a volume
 * whose features this implementation does not have.  That is what the
 * feature words are for: an unknown incompat bit means the volume holds
 * something this code would silently get wrong.
 */
  if (d->s_magic != MFS4_SUPER_MAGIC)
	return EINVAL;

  memset(sp, 0, offsetof(struct super_block, s_inodes_per_block));

  sp->s_ninodes = d->s_ninodes;
  sp->s_zones = d->s_nblocks;
  sp->s_imap_blocks = d->s_imap_blocks;
  sp->s_zmap_blocks = d->s_zmap_blocks;
  sp->s_firstdatazone = d->s_firstdatablock;
  sp->s_log_zone_size = 0;
  sp->s_flags = d->s_flags;
  sp->s_max_size = (off_t) d->s_max_size;
  sp->s_block_size = d->s_block_size;
  sp->s_inode_size = d->s_inode_size;
  sp->s_feature_compat = d->s_feature_compat;
  sp->s_feature_ro_compat = d->s_feature_ro_compat;
  sp->s_feature_incompat = d->s_feature_incompat;
  sp->s_mkfs_time = (time_t) d->s_mkfs_time;
  sp->s_mount_time = (time_t) d->s_mount_time;
  sp->s_write_time = (time_t) d->s_write_time;
  sp->s_journal_inum = d->s_journal_inum;
  sp->s_journal_blocks = d->s_journal_blocks;
  memcpy(sp->s_uuid, d->s_uuid, sizeof(sp->s_uuid));
  memcpy(sp->s_label, d->s_label, sizeof(sp->s_label));
  sp->s_version = V4;
  sp->s_native = 1;

  if (d->s_disk_version != 0) {
	printf("MFS: V4 sub-version %u is not known here\n",
	    d->s_disk_version);
	return EINVAL;
  }

  if (sp->s_feature_incompat & ~(u32_t) MFS4_INCOMPAT_SUPP) {
	printf("MFS: filesystem has incompatible features 0x%x; "
	    "please use a newer MFS to mount it\n",
	    sp->s_feature_incompat & ~(u32_t) MFS4_INCOMPAT_SUPP);
	return EINVAL;
  }

  if (sp->s_feature_ro_compat & ~(u32_t) MFS4_RO_COMPAT_SUPP) {
	printf("MFS: filesystem has features 0x%x this MFS cannot write; "
	    "mounting read-only\n",
	    sp->s_feature_ro_compat & ~(u32_t) MFS4_RO_COMPAT_SUPP);
	sp->s_rd_only = 1;
  }

  /* The inode size is a field, so that a wider inode is a number here and
   * a feature bit, rather than a fifth version of the format.
   */
  if (sp->s_inode_size < MFS4_INODE_SIZE ||
      sp->s_block_size % sp->s_inode_size != 0) {
	printf("MFS: bad inode size %u\n", sp->s_inode_size);
	return EINVAL;
  }

  sp->s_inodes_per_block = sp->s_block_size / sp->s_inode_size;
  sp->s_ndzones = MFS4_NR_DZONES;
  sp->s_nindirs = sp->s_block_size / sizeof(zone_t);
  sp->s_nlevels = MFS4_NR_LEVELS;
  sp->s_name_max = MFS4_NAME_MAX;

  return OK;
}

/*===========================================================================*
 *				super_to_v4				     *
 *===========================================================================*/
static void super_to_v4(const struct super_block *sp, struct mfs4_super *d)
{
  memset(d, 0, sizeof(*d));

  d->s_magic = MFS4_SUPER_MAGIC;
  d->s_disk_version = 0;
  d->s_block_size = sp->s_block_size;
  d->s_ninodes = sp->s_ninodes;
  d->s_nblocks = sp->s_zones;
  d->s_firstdatablock = sp->s_firstdatazone;
  d->s_inode_size = sp->s_inode_size;
  d->s_imap_blocks = sp->s_imap_blocks;
  d->s_zmap_blocks = sp->s_zmap_blocks;
  d->s_flags = sp->s_flags;
  d->s_feature_compat = sp->s_feature_compat;
  d->s_feature_ro_compat = sp->s_feature_ro_compat;
  d->s_feature_incompat = sp->s_feature_incompat;
  d->s_max_size = (uint64_t) sp->s_max_size;
  d->s_mkfs_time = (int64_t) sp->s_mkfs_time;
  d->s_mount_time = (int64_t) sp->s_mount_time;
  d->s_write_time = (int64_t) sp->s_write_time;
  d->s_journal_inum = sp->s_journal_inum;
  d->s_journal_blocks = sp->s_journal_blocks;
  memcpy(d->s_uuid, sp->s_uuid, sizeof(d->s_uuid));
  memcpy(d->s_label, sp->s_label, sizeof(d->s_label));
}

/*===========================================================================*
 *				rw_super				     *
 *===========================================================================*/
static int rw_super(struct super_block *sp, int writing)
{
/* Read or write a superblock.  On disk it is one of two structures, at byte
 * offset SUPER_BLOCK_BYTES; in core it is a structure of the server's own,
 * and the four functions above convert.
 */
  dev_t save_dev = sp->s_dev;
  struct buf *bp;
  char *sbbuf;
  int r;

  if (sp->s_dev == NO_DEV)
  	panic("request for super_block of NO_DEV");

  /* we rely on the cache blocksize, before reading the
   * superblock, being big enough that our complete superblock
   * is in block 0.
   */
  assert(lmfs_fs_block_size() >= SUPER_BLOCK_BYTES + MFS4_SUPER_SIZE);
  assert(MFS4_SUPER_SIZE >= sizeof(struct mfs4_super));
  assert(SUPER_BLOCK_BYTES >= sizeof(struct mfs3_super));

  /* Unlike accessing any other block, failure to read the superblock is a
   * somewhat legitimate use case: it may happen when trying to mount a
   * zero-sized partition.  In that case, we'd rather faily cleanly than
   * crash the MFS service.
   */
  if ((r = lmfs_get_block(&bp, sp->s_dev, 0, NORMAL)) != OK) {
	if (writing)
		panic("get_block of superblock failed: %d", r);
	else
		return r;
  }

  /* sbbuf points to the disk block at the superblock offset */
  sbbuf = (char *) b_data(bp) + SUPER_BLOCK_BYTES;

  if (writing) {
	memset(b_data(bp), 0, lmfs_fs_block_size());

	if (sp->s_version == V4) {
		struct mfs4_super d4;

		super_to_v4(sp, &d4);
		memcpy(sbbuf, &d4, sizeof(d4));
	} else {
		struct mfs3_super d3;

		super_to_v3(sp, &d3);
		memcpy(sbbuf, &d3, sizeof(d3));
	}
	lmfs_markdirty(bp);
	r = OK;
  } else {
	uint32_t magic4;

	memcpy(&magic4, sbbuf, sizeof(magic4));

	if (magic4 == MFS4_SUPER_MAGIC) {
		struct mfs4_super d4;

		memcpy(&d4, sbbuf, sizeof(d4));
		r = super_from_v4(sp, &d4);
	} else {
		struct mfs3_super d3;

		memcpy(&d3, sbbuf, sizeof(d3));
		r = super_from_v3(sp, &d3);
	}
  }

  put_block(bp);

  sp->s_dev = save_dev;

  return r;
}

/*===========================================================================*
 *				read_super				     *
 *===========================================================================*/
int read_super(struct super_block *sp)
{
/* Read a superblock of either version and check that it makes sense. */
  int r;

  if ((r = rw_super(sp, 0)) != OK)
	return r;

  if (sp->s_block_size < PAGE_SIZE)
	return EINVAL;
  if ((sp->s_block_size % 512) != 0)
	return EINVAL;
  if (sp->s_block_size % sp->s_inode_size != 0)
	return EINVAL;
  if (sp->s_version == V4 && sp->s_block_size > MFS4_MAX_BLOCK_SIZE)
	return EINVAL;

  /* Make a few basic checks to see if super block looks reasonable. */
  if (sp->s_imap_blocks < 1 || sp->s_zmap_blocks < 1
				|| sp->s_ninodes < 1 || sp->s_zones < 1
				|| sp->s_firstdatazone <= 4
				|| sp->s_firstdatazone >= sp->s_zones
				|| sp->s_log_zone_size > 4) {
  	printf("not enough imap or zone map blocks, \n");
  	printf("or not enough inodes, or not enough zones, \n"
  		"or invalid first data zone, or zone size too large\n");
	return(EINVAL);
  }

  /* V3 has no feature words; any of these bits it does not understand is
   * fatal.  V4 has three, and super_from_v4() has already checked them.
   */
  if (sp->s_version == V3 && (sp->s_flags & MFS3_FLAG_MANDATORY_MASK)) {
  	printf("MFS: unsupported feature flags on this FS.\n"
		"Please use a newer MFS to mount it.\n");
	return(EINVAL);
  }

  return(OK);
}

/*===========================================================================*
 *				write_super				     *
 *===========================================================================*/
int write_super(struct super_block *sp)
{
  if(sp->s_rd_only)
  	panic("can't write superblock of readonly filesystem");

  if (sp->s_version == V4)
	sp->s_write_time = clock_time(NULL);

  return rw_super(sp, 1);
}
