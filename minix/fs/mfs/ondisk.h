#ifndef __MFS_ONDISK_H__
#define __MFS_ONDISK_H__

/*
 * What MFS puts on a disk: the layouts of V3 and of V4, and nothing else.
 *
 * This header is included by the server, by mkfs.mfs - which is built both
 * for the system and as a host tool - and, in time, by fsck. So it names
 * only <stdint.h> types and defines no policy: a field's meaning is here,
 * the decision of what to write in it is in the program.
 *
 * The design of V4 and the reasoning behind each field is in
 * port/PORTING-LOG.md, "Этап 7.3".
 *
 * Byte order on disk is little-endian, and only little-endian. V3 carried
 * conversion routines with a "native" flag that was unconditionally true;
 * the code they were for never ran. A big-endian host would have to convert
 * in three places (superblock, inode, directory entry); until one exists,
 * the structures are read as they lie, and a foreign volume is rejected by
 * its magic number rather than misread.
 */

#include <stdint.h>

/*===========================================================================*
 *				V3					     *
 *===========================================================================*/

/*
 * The V3 superblock, at byte offset 1024. This is the layout the field
 * order and the types of the historical in-core struct produced; it is
 * written out explicitly here so that the in-core superblock can be a
 * structure of the server's own, holding what both versions need.
 */
struct mfs3_super {
	uint32_t s_ninodes;		/* usable inodes on the device */
	uint16_t s_nzones;		/* V1/V2 zone count; 0 in V3 */
	int16_t  s_imap_blocks;		/* blocks of the inode bitmap */
	int16_t  s_zmap_blocks;		/* blocks of the zone bitmap */
	uint16_t s_firstdatazone_old;	/* first data zone, or 0 if too big */
	int16_t  s_log_zone_size;	/* log2 blocks per zone; 0 in V3 */
	uint16_t s_flags;		/* MFS3_FLAG_* */
	int32_t  s_max_size;		/* largest file this FS can hold */
	uint32_t s_zones;		/* zones (= blocks) on the device */
	int16_t  s_magic;		/* MFS3_SUPER_MAGIC */
	int16_t  s_pad2;
	uint16_t s_block_size;		/* bytes per block */
	uint8_t  s_disk_version;	/* format sub-version */
};

#define MFS3_SUPER_MAGIC	0x4d5a	/* V3 */
#define MFS3_SUPER_MAGIC_V2	0x2468	/* V2, refused */
#define MFS3_SUPER_MAGIC_V1	0x137F	/* V1, refused */

#define MFS3_FLAG_CLEAN		0x0001	/* unmounted cleanly */
/*
 * Any flag in this mask that the implementation does not know is fatal:
 * V3's whole feature mechanism, replaced in V4 by three feature words.
 */
#define MFS3_FLAG_MANDATORY_MASK 0xff00

#define MFS3_NR_DZONES		7	/* direct zones in a V3 inode */
#define MFS3_NR_TZONES		10	/* total zone slots in a V3 inode */
#define MFS3_NR_LEVELS		2	/* single and double indirect */

/* The V3 inode, 64 bytes. */
struct mfs3_inode {
	uint16_t d2_mode;
	uint16_t d2_nlinks;
	int16_t  d2_uid;
	uint16_t d2_gid;
	int32_t  d2_size;
	int32_t  d2_atime;
	int32_t  d2_mtime;
	int32_t  d2_ctime;
	uint32_t d2_zone[MFS3_NR_TZONES];
};

/* The V3 directory entry: fixed 64 bytes, name truncated at 60. */
#define MFS3_DIRSIZ	60

struct mfs3_dirent {
	uint32_t mfs_d_ino;
	char mfs_d_name[MFS3_DIRSIZ];
} __packed;

/*===========================================================================*
 *				V4					     *
 *===========================================================================*/

/*
 * The V4 superblock, at byte offset 1024, 256 bytes reserved.
 *
 * The magic is at offset 0 rather than at 24 where V3 keeps it, and what
 * lies at 24 here is s_inode_size = 128. So a V3 server reading a V4
 * volume finds 0x0080 where it wants its magic and says "bad magic"
 * instead of trying to make sense of the rest; and a V4 server reading a
 * V3 volume finds an inode count at offset 0, which cannot collide with
 * the V4 magic (that many inodes would be a 50 TB inode table).
 */
struct mfs4_super {
	uint32_t s_magic;		/* MFS4_SUPER_MAGIC */
	uint32_t s_disk_version;	/* 0 */
	uint32_t s_block_size;		/* bytes per block */
	uint32_t s_ninodes;		/* inodes, root included */
	uint32_t s_nblocks;		/* blocks on the device */
	uint32_t s_firstdatablock;	/* first block of data */
	uint32_t s_inode_size;		/* bytes per inode; 128 today */
	uint32_t s_imap_blocks;
	uint32_t s_zmap_blocks;
	uint32_t s_flags;		/* MFS4_FLAG_* */
	uint32_t s_feature_compat;
	uint32_t s_feature_ro_compat;
	uint32_t s_feature_incompat;
	uint32_t s_reserved0;
	uint64_t s_max_size;		/* largest file this FS can hold */
	int64_t  s_mkfs_time;
	int64_t  s_mount_time;		/* last mount for writing */
	int64_t  s_write_time;		/* last superblock write */
	uint32_t s_journal_inum;	/* inode holding the journal, or 0 */
	uint32_t s_journal_blocks;
	uint8_t  s_uuid[16];
	char     s_label[16];
	uint8_t  s_reserved1[128];
};

#define MFS4_SUPER_MAGIC	0x3453464dUL	/* "MFS4" little-endian */
#define MFS4_SUPER_SIZE		256

#define MFS4_FLAG_CLEAN		0x0001	/* unmounted cleanly */

/*
 * Feature words, with ext2's rules, which is the whole point of having
 * them: an unknown incompat bit means do not mount, an unknown ro_compat
 * bit means mount read-only, an unknown compat bit means carry on. Every
 * future step - a journal, hashed directories, checksums - is a bit here
 * rather than a new version number.
 */
#define MFS4_COMPAT_HAS_JOURNAL		0x00000001
#define MFS4_COMPAT_SUPP		(MFS4_COMPAT_HAS_JOURNAL)

#define MFS4_RO_COMPAT_SUPP		0

#define MFS4_INCOMPAT_RECOVER		0x00000001
#define MFS4_INCOMPAT_SUPP		(MFS4_INCOMPAT_RECOVER)

/* Blocks are addressed by a 32-bit number: 16 TB at a 4 KB block. */
#define MFS4_NR_DZONES		12	/* direct block numbers in an inode */
#define MFS4_NR_TZONES		15	/* plus single, double, triple */
#define MFS4_NR_LEVELS		3	/* levels of indirection */

#define MFS4_INODE_SIZE		128

/*
 * The V4 inode. The arithmetic of 128 bytes came out four bytes short, and
 * what was given up is a nanosecond field for i_btime: a creation time is
 * never compared with anything finer than a second, while i_flags is the
 * room without which an immutable bit would be a new format.
 */
struct mfs4_inode {
	uint16_t i_mode;
	uint16_t i_flags;		/* reserved: immutable, append-only */
	uint32_t i_nlinks;
	uint32_t i_uid;
	uint32_t i_gid;
	uint64_t i_size;
	int64_t  i_atime;
	int64_t  i_mtime;
	int64_t  i_ctime;
	int64_t  i_btime;		/* creation */
	uint32_t i_atime_nsec;
	uint32_t i_mtime_nsec;
	uint32_t i_ctime_nsec;
	uint32_t i_zone[MFS4_NR_TZONES];
};

/*
 * A symbolic link whose target fits in the block-number array is stored
 * there, as ext2 does it. The rule is by size alone, so that mkfs, the
 * server and fsck agree without a flag; fsck must not read these bytes as
 * block numbers.
 */
#define MFS4_FAST_SYMLINK_MAX	(MFS4_NR_TZONES * sizeof(uint32_t))

/*
 * The V4 directory entry: variable length, as in ext2 and UFS. Entries do
 * not cross a block boundary, the last entry of a block has its d_rec_len
 * reaching the end of the block, and a directory's size is a whole number
 * of blocks. A free entry has d_ino == 0 and keeps its d_rec_len.
 *
 * d_type is what makes readdir cheap: V3 had to read the inode of every
 * entry to answer with a type, which its own source calls "seriously
 * expensive". The values are <sys/dirent.h>'s DT_*.
 */
struct mfs4_dirent {
	uint32_t d_ino;			/* 0: this entry is free */
	uint16_t d_rec_len;		/* bytes to the next entry */
	uint8_t  d_name_len;		/* 1..255 */
	uint8_t  d_type;		/* DT_* */
	char     d_name[];		/* not NUL-terminated */
};

#define MFS4_DIRENT_HDR		8	/* bytes before the name */
#define MFS4_NAME_MAX		255
#define MFS4_DIRENT_ALIGN	8

/* The space one entry with a name of this length occupies. */
#define MFS4_DIRENT_LEN(namelen) \
	(((MFS4_DIRENT_HDR + (namelen)) + MFS4_DIRENT_ALIGN - 1) & \
	    ~(MFS4_DIRENT_ALIGN - 1))

/*
 * A block size below a page is refused by the block cache; above 32 KB a
 * record length would not fit in d_rec_len.
 */
#define MFS4_MIN_BLOCK_SIZE	4096
#define MFS4_MAX_BLOCK_SIZE	32768

#endif /* __MFS_ONDISK_H__ */
