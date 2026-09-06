
#ifndef _MFS_CLEAN_H
#define _MFS_CLEAN_H 1

/*
 * A block of metadata: an inode, a bitmap, a directory, an indirect block.
 * On a file system with a journal these go through it, which is what makes
 * a crash repairable; without one this is the plain markdirty it always was.
 *
 * Almost every block this file system marks dirty is metadata, so this is
 * the default and the three places that write file data say so instead.
 */
#define MARKDIRTY(b) do { \
	if (superblock.s_rd_only) { \
		printf("%s:%d: dirty block on rofs! ", __FILE__, __LINE__); \
		util_stacktrace(); \
	} else { \
		lmfs_markdirty_meta(b); \
	} \
} while(0)

/* A block of file data: it goes to its place on the disk, and in ordered
 * mode it goes there before the metadata that points at it is committed.
 */
#define MARKDIRTY_DATA(b) do { \
	if (superblock.s_rd_only) { \
		printf("%s:%d: dirty block on rofs! ", __FILE__, __LINE__); \
		util_stacktrace(); \
	} else { \
		lmfs_markdirty(b); \
	} \
} while(0)

#endif
