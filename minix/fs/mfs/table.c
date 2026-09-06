
/* This file contains the table used to map file system calls onto the
 * routines that perform them.
 *
 * The calls that change the file system are wrapped in a transaction.  What
 * that does is not to start anything - a transaction opens itself when the
 * first metadata block is marked dirty - but to say where an operation ends,
 * which is the only place the journal may be committed.  A transaction that
 * held half of an operation would be a journal that replays half of one.
 */

#define _TABLE

#include "fs.h"
#include "inode.h"
#include "buf.h"
#include "super.h"

/*
 * One wrapper per call that can change the file system.  They are dull on
 * purpose: the alternative is a begin and an end inside each operation,
 * which have to be got right at every return statement.
 */
#define TXN_WRAP(name, proto, args)					\
static int txn_ ## name proto						\
{									\
	int r;								\
									\
	lmfs_txn_begin();						\
	r = fs_ ## name args;						\
	lmfs_txn_end();							\
									\
	return r;							\
}

TXN_WRAP(putnode, (ino_t ino_nr, unsigned int count), (ino_nr, count))
TXN_WRAP(trunc, (ino_t ino_nr, off_t start, off_t end), (ino_nr, start, end))
TXN_WRAP(create, (ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	struct fsdriver_node *node), (dir_nr, name, mode, uid, gid, node))
TXN_WRAP(mkdir, (ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid),
	(dir_nr, name, mode, uid, gid))
TXN_WRAP(mknod, (ino_t dir_nr, char *name, mode_t mode, uid_t uid, gid_t gid,
	dev_t dev), (dir_nr, name, mode, uid, gid, dev))
TXN_WRAP(link, (ino_t dir_nr, char *name, ino_t ino_nr),
	(dir_nr, name, ino_nr))
TXN_WRAP(unlink, (ino_t dir_nr, char *name, int call), (dir_nr, name, call))
TXN_WRAP(rename, (ino_t old_dir_nr, char *old_name, ino_t new_dir_nr,
	char *new_name), (old_dir_nr, old_name, new_dir_nr, new_name))
TXN_WRAP(slink, (ino_t dir_nr, char *name, uid_t uid, gid_t gid,
	struct fsdriver_data *data, size_t bytes),
	(dir_nr, name, uid, gid, data, bytes))
TXN_WRAP(chown, (ino_t ino_nr, uid_t uid, gid_t gid, mode_t *mode),
	(ino_nr, uid, gid, mode))
TXN_WRAP(chmod, (ino_t ino_nr, mode_t *mode), (ino_nr, mode))
TXN_WRAP(utime, (ino_t ino_nr, struct timespec *atime, struct timespec *mtime),
	(ino_nr, atime, mtime))

static ssize_t txn_readwrite(ino_t ino_nr, struct fsdriver_data *data,
	size_t bytes, off_t pos, int call)
{
	ssize_t r;

	lmfs_txn_begin();
	r = fs_readwrite(ino_nr, data, bytes, pos, call);
	lmfs_txn_end();

	return r;
}

struct fsdriver mfs_table = {
	.fdr_mount	= fs_mount,
	.fdr_unmount	= fs_unmount,
	.fdr_lookup	= fs_lookup,
	.fdr_putnode	= txn_putnode,
	.fdr_read	= txn_readwrite,
	.fdr_write	= txn_readwrite,
	.fdr_peek	= txn_readwrite,
	.fdr_getdents	= fs_getdents,
	.fdr_trunc	= txn_trunc,
	.fdr_seek	= fs_seek,
	.fdr_create	= txn_create,
	.fdr_mkdir	= txn_mkdir,
	.fdr_mknod	= txn_mknod,
	.fdr_link	= txn_link,
	.fdr_unlink	= txn_unlink,
	.fdr_rmdir	= txn_unlink,
	.fdr_rename	= txn_rename,
	.fdr_slink	= txn_slink,
	.fdr_rdlink	= fs_rdlink,
	.fdr_stat	= fs_stat,
	.fdr_chown	= txn_chown,
	.fdr_chmod	= txn_chmod,
	.fdr_utime	= txn_utime,
	.fdr_mountpt	= fs_mountpt,
	.fdr_statvfs	= fs_statvfs,
	.fdr_sync	= fs_sync,
	.fdr_driver	= lmfs_driver,
	.fdr_bread	= lmfs_bio,
	.fdr_bwrite	= lmfs_bio,
	.fdr_bpeek	= lmfs_bio,
	.fdr_bflush	= lmfs_bflush
};
