#ifndef __MFS_CONST_H__
#define __MFS_CONST_H__

#include "ondisk.h"

/* Tables sizes */
#define V2_NR_DZONES   MFS3_NR_DZONES	/* # direct zone numbers in a V3 inode */
#define V2_NR_TZONES   MFS3_NR_TZONES	/* total # zone numbers in a V3 inode */

/* The widest an in-core inode has to be: V4's, which has the more of both. */
#define NR_TZONES	MFS4_NR_TZONES

#define NR_INODES        512	/* # slots in "in core" inode table,
				 * should be more or less the same as
				 * NR_VNODES in vfs
				 */

#define INODE_HASH_LOG2   7     /* 2 based logarithm of the inode hash size */
#define INODE_HASH_SIZE   ((unsigned long)1<<INODE_HASH_LOG2)
#define INODE_HASH_MASK   (((unsigned long)1<<INODE_HASH_LOG2)-1)

/* Max. filename length, per version; the mount fills in s_name_max. */
#define MFS_NAME_MAX	 MFS4_NAME_MAX

/* File system types. */
#define SUPER_MAGIC   MFS3_SUPER_MAGIC_V1  /* magic number of a V1 super-block*/
#define SUPER_V2      MFS3_SUPER_MAGIC_V2  /* magic # for V2 file systems */
#define SUPER_V3      MFS3_SUPER_MAGIC	   /* magic # for V3 file systems */
#define SUPER_V4      MFS4_SUPER_MAGIC	   /* magic # for V4 file systems */

#define V2		   2	/* version number of V2 file systems */
#define V3		   3	/* version number of V3 file systems */
#define V4		   4	/* version number of V4 file systems */

/* Miscellaneous constants */
#define NO_BIT   ((bit_t) 0)	/* returned by alloc_bit() to signal failure */

#define LOOK_UP            0 /* tells search_dir to lookup string */
#define ENTER              1 /* tells search_dir to make dir entry */
#define DELETE             2 /* tells search_dir to delete entry */
#define IS_EMPTY           3 /* tells search_dir to ret. OK or ENOTEMPTY */

/* write_map() args */
#define WMAP_FREE	(1 << 0)

#define IN_CLEAN        0	/* in-block inode and memory copies identical */
#define IN_DIRTY        1	/* in-block inode and memory copies differ */
#define ATIME            002	/* set if atime field needs updating */
#define CTIME            004	/* set if ctime field needs updating */
#define MTIME            010	/* set if mtime field needs updating */

#define ROOT_INODE   ((ino_t) 1)	/* inode number for root directory */
#define BOOT_BLOCK  ((block_t) 0)	/* block number of boot block */
#define SUPER_BLOCK_BYTES  (1024)	/* bytes offset */
#define START_BLOCK ((block_t) 2)	/* first block of FS (not counting SB) */

/* Directory entries.  V3's are fixed-size, so a block holds a whole number
 * of them; V4's are variable-length and walked by their record lengths.
 */
#define DIR_ENTRY_SIZE     sizeof (struct mfs3_dirent) /* # bytes/dir entry  */
#define NR_DIR_ENTRIES(b)   ((b)/DIR_ENTRY_SIZE)  /* # dir entries/blk   */

#define FS_BITMAP_CHUNKS(b) ((b)/sizeof (bitchunk_t))/* # map chunks/blk   */
#define FS_BITCHUNK_BITS		(sizeof(bitchunk_t) * CHAR_BIT)
#define FS_BITS_PER_BLOCK(b)	(FS_BITMAP_CHUNKS(b) * FS_BITCHUNK_BITS)

/* Derived sizes pertaining to the V3 file system. */
#define V2_ZONE_NUM_SIZE            sizeof (zone_t)  /* # bytes in V2 zone  */
#define V2_INODE_SIZE            sizeof (d2_inode)  /* bytes in V3 dsk ino */
#define V2_INDIRECTS(b)   ((b)/V2_ZONE_NUM_SIZE)  /* # zones/indir block */
#define V2_INODES_PER_BLOCK(b) ((b)/V2_INODE_SIZE)/* # V3 dsk inodes/blk */

/* And to the V4 file system.  Its indirect blocks hold the same 32-bit
 * block numbers, so the count per block is the same arithmetic.
 */
#define V4_INODE_SIZE       sizeof (struct mfs4_inode)
#define V4_INODES_PER_BLOCK(b) ((b)/V4_INODE_SIZE)

#endif
