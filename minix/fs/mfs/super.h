#ifndef __MFS_SUPER_H__
#define __MFS_SUPER_H__

/* The in-core superblock: what the server needs to know about the mounted
 * file system, whichever version it is written in.  The layouts on disk are
 * in ondisk.h, and read_super() converts between them and this; the two used
 * to be one structure, which worked only for as long as there was one format.
 *
 * The disk layout of both versions is:
 *
 *    Item        # blocks
 *    boot block      1
 *    super block     1    (offset 1kB)
 *    inode map     s_imap_blocks
 *    zone map      s_zmap_blocks
 *    inodes        (s_ninodes + 'inodes per block' - 1)/'inodes per block'
 *    unused        whatever is needed to fill out the current zone
 *    data zones    s_zones - s_firstdatazone
 *
 * A zone is a block: V3 requires s_log_zone_size to be zero, and V4 has no
 * such field.  The word "zone" survives as the name of the block bitmap and
 * of the numbers stored in an inode.
 *
 * A super_block slot is free if s_dev == NO_DEV.
 */

EXTERN struct super_block {
  /* Fields both versions have, in the server's own types. */
  u32_t s_ninodes;		/* # usable inodes on the minor device */
  zone_t s_zones;		/* total device size in blocks */
  u32_t s_imap_blocks;		/* # of blocks used by inode bit map */
  u32_t s_zmap_blocks;		/* # of blocks used by zone bit map */
  zone_t s_firstdatazone;	/* number of first data zone */
  u32_t s_log_zone_size;	/* log2 of blocks/zone; always 0 */
  u32_t s_flags;		/* FS state flags */
  off_t s_max_size;		/* maximum file size on this device */
  u32_t s_block_size;		/* block size in bytes */
  u32_t s_inode_size;		/* bytes per inode on disk */

  /* V4 only; zero on a V3 file system. */
  u32_t s_feature_compat;
  u32_t s_feature_ro_compat;
  u32_t s_feature_incompat;
  time_t s_mkfs_time;
  time_t s_mount_time;
  time_t s_write_time;
  u32_t s_journal_inum;
  u32_t s_journal_blocks;
  u8_t s_uuid[16];
  char s_label[16];

  /* Not on disk: derived at mount time, or state of this mount. */
  unsigned s_inodes_per_block;	/* precalculated from the inode size */
  dev_t s_dev;			/* whose super block is this? */
  int s_rd_only;		/* set to 1 iff file sys mounted read only */
  int s_native;			/* set to 1 iff not byte swapped file system */
  int s_version;		/* V3 or V4; zero means bad magic */
  int s_ndzones;		/* # direct zones in an inode */
  int s_nindirs;		/* # indirect zones per indirect block */
  int s_nlevels;		/* # levels of indirection in an inode */
  unsigned s_name_max;		/* longest name a directory entry can hold */
  bit_t s_isearch;		/* inodes below this bit number are in use */
  bit_t s_zsearch;		/* all zones below this bit number are in use*/
} superblock;

#define IMAP		0	/* operating on the inode bit map */
#define ZMAP		1	/* operating on the zone bit map */

/* s_flags contents, the same bit in both versions. */
#define MFSFLAG_CLEAN	MFS3_FLAG_CLEAN	/* 0: dirty; 1: unmounted cleanly */

#endif
