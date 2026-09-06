#ifndef __MFS_INODE_H__
#define __MFS_INODE_H__

/* Inode table.  This table holds inodes that are currently in use.  In some
 * cases they have been opened by an open() or creat() system call, in other
 * cases the file system itself needs the inode for one reason or another,
 * such as to search a directory for a path name.
 *
 * The first part of the struct holds fields that are present on the disk -
 * as wide as the wider of the two formats, V4, has them: a V3 inode is read
 * into the same fields and written back narrowed.  The second part holds
 * fields not present on the disk.
 *
 * Updates:
 * 2007-01-06: jfdsmit@gmail.com added i_zsearch
 */

#include <sys/queue.h>
#include <minix/vfsif.h>

#include "super.h"

EXTERN struct inode {
  u16_t i_mode;			/* file type, protection, etc. */
  u16_t i_flags;		/* V4: reserved for per-file flags */
  u32_t i_nlinks;		/* how many links to this file */
  uid_t i_uid;			/* user id of the file's owner */
  gid_t i_gid;			/* group number */
  off_t i_size;			/* current file size in bytes */
  time_t i_atime;		/* time of last access */
  time_t i_mtime;		/* when was file data last changed */
  time_t i_ctime;		/* when was inode itself changed */
  time_t i_btime;		/* V4: when was the file created */
  u32_t i_atime_nsec;		/* V4: subsecond part of the three times */
  u32_t i_mtime_nsec;
  u32_t i_ctime_nsec;
  u32_t i_zone[NR_TZONES];	/* block numbers: direct, then one per level
				 * of indirection.  A fast symbolic link keeps
				 * its target in these bytes instead.
				 */

  /* The following items are not present on the disk. */
  dev_t i_dev;			/* which device is the inode on */
  ino_t i_num;			/* inode number on its (minor) device */
  int i_count;			/* # times inode used; 0 means slot is free */
  unsigned int i_ndzones;	/* # direct zones (Vx_NR_DZONES) */
  unsigned int i_nindirs;	/* # indirect zones per indirect block */
  unsigned int i_nlevels;	/* # levels of indirection in this format */
  struct super_block *i_sp;	/* pointer to super block for inode's device */
  char i_dirt;			/* CLEAN or DIRTY */
  zone_t i_zsearch;		/* where to start search for new zones */
  off_t i_last_dpos;		/* where to start dentry search */

  char i_mountpoint;		/* true if mounted on */

  char i_seek;			/* set on LSEEK, cleared on READ/WRITE */
  char i_update;		/* the ATIME, CTIME, and MTIME bits are here */

  LIST_ENTRY(inode) i_hash;     /* hash list */
  TAILQ_ENTRY(inode) i_unused;  /* free and unused list */

} inode[NR_INODES];

/* list of unused/free inodes */
EXTERN TAILQ_HEAD(unused_inodes_t, inode)  unused_inodes;

/* inode hashtable */
EXTERN LIST_HEAD(inodelist, inode)         hash_inodes[INODE_HASH_SIZE];

EXTERN unsigned int inode_cache_hit;
EXTERN unsigned int inode_cache_miss;


/* Field values.  Note that CLEAN and DIRTY are defined in "const.h" */
#define NO_SEEK            0	/* i_seek = NO_SEEK if last op was not SEEK */
#define ISEEK              1	/* i_seek = ISEEK if last op was SEEK */

#define IN_MARKCLEAN(i) i->i_dirt = IN_CLEAN
#define IN_MARKDIRTY(i) do { if(i->i_sp->s_rd_only) { printf("%s:%d: dirty inode on rofs ", __FILE__, __LINE__); util_stacktrace(); } else { i->i_dirt = IN_DIRTY; } } while(0)

#define IN_ISCLEAN(i) i->i_dirt == IN_CLEAN
#define IN_ISDIRTY(i) i->i_dirt == IN_DIRTY

/* A symbolic link short enough to live in the block-number array does; the
 * rule is by size alone, so that every reader agrees without a flag.  Only
 * V4 has them: a V3 link always has a block of its own.
 */
#define IS_FAST_SYMLINK(rip) \
	((rip)->i_sp->s_version == V4 && S_ISLNK((rip)->i_mode) && \
	 (rip)->i_size > 0 && (rip)->i_size <= (off_t) MFS4_FAST_SYMLINK_MAX)

#endif
