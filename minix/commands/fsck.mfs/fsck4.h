#ifndef _FSCK4_H
#define _FSCK4_H

/*
 * The checker for MFS V4.  It is a separate body of code from the V3 one -
 * the two formats share no inode, no directory entry and no block map - but
 * it is part of the same program: what calls fsck_mfs is rc, mount(8) and
 * NetBSD's fsck(8), and none of them should have to know which version of
 * the format a volume is written in.
 */

/* What the caller allows this checker to do. */
#define FSCK4_REPAIR	0x01	/* may write to the file system */
#define FSCK4_AUTO	0x02	/* answer every question with yes */
#define FSCK4_PREEN	0x04	/* say little, and skip a clean volume */
#define FSCK4_LISTING	0x08	/* list what is found */

/* Is this a V4 file system?  Reads the superblock; leaves the offset
 * wherever it likes, so the caller should not care about it.
 */
int fsck4_is_v4(int fd);

/* Check (and, if allowed, repair) it.  Returns an FSCK_EXIT_* code. */
int fsck4_check(int fd, const char *devname, int flags);

#endif /* _FSCK4_H */
