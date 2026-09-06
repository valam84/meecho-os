/* Prototypes for -lminixfs. */

#ifndef _MINIX_FSLIB_H
#define _MINIX_FSLIB_H

#include <minix/fsdriver.h>

/* Maximum number of blocks that will be considered by lmfs_prefetch() */
#define LMFS_MAX_PREFETCH	NR_IOREQS

struct buf {
  /* Data portion of the buffer. */
  void *data;

  /* Header portion of the buffer - internal to libminixfs. */
  struct buf *lmfs_next;       /* used to link all free bufs in a chain */
  struct buf *lmfs_prev;       /* used to link all free bufs the other way */
  struct buf *lmfs_hash;       /* used to link bufs on hash chains */
  dev_t lmfs_dev;              /* major | minor device where block resides */
  block64_t lmfs_blocknr;      /* block number of its (minor) device */
  char lmfs_count;             /* number of users of this buffer */
  char lmfs_needsetcache;      /* to be identified to VM */
  char lmfs_journaled;         /* in the open transaction, and held by it */
  size_t lmfs_bytes;           /* size of this block (allocated and used) */
  u32_t lmfs_flags;            /* Flags shared between VM and FS */

  /* If any, which inode & offset does this block correspond to?
   * If none, VMC_NO_INODE
   */
  ino_t lmfs_inode;
  u64_t lmfs_inode_offset;
};

void lmfs_markdirty(struct buf *bp);
void lmfs_markdirty_meta(struct buf *bp);
void lmfs_markclean(struct buf *bp);
int lmfs_isclean(struct buf *bp);
void lmfs_flushall(void);
void lmfs_flushdev(dev_t dev);
size_t lmfs_fs_block_size(void);
void lmfs_may_use_vmcache(int);
void lmfs_set_blocksize(size_t blocksize);
void lmfs_buf_pool(int new_nr_bufs);
int lmfs_get_block(struct buf **bpp, dev_t dev, block64_t block, int how);
int lmfs_get_block_ino(struct buf **bpp, dev_t dev, block64_t block, int how,
	ino_t ino, u64_t off);
void lmfs_put_block(struct buf *bp);
void lmfs_free_block(dev_t dev, block64_t block);
void lmfs_zero_block_ino(dev_t dev, ino_t ino, u64_t off);
void lmfs_invalidate(dev_t device);
void lmfs_prefetch(dev_t dev, const block64_t *blockset, unsigned int nblocks);
void lmfs_setquiet(int q);
void lmfs_set_blockusage(fsblkcnt_t btotal, fsblkcnt_t bused);
void lmfs_change_blockusage(int delta);

/* get_block arguments */
#define NORMAL             0    /* forces get_block to do disk read */
#define NO_READ            1    /* prevents get_block from doing disk read */
#define PEEK               2    /* returns ENOENT if not in cache */

/*
 * The metadata journal.  A file system that has one replays it at mount,
 * hands it to the library, marks its metadata blocks with
 * lmfs_markdirty_meta() instead of lmfs_markdirty(), and brackets each of
 * its operations in lmfs_txn_begin()/lmfs_txn_end().  A file system that
 * has none does none of this and behaves as it always did.
 */
int lmfs_journal_replay(dev_t dev, block64_t start, unsigned int nblocks,
	size_t bsize, const uint8_t *uuid, unsigned int *replayed);
int lmfs_journal_init(dev_t dev, block64_t start, unsigned int nblocks,
	size_t bsize, const uint8_t *uuid);
int lmfs_journal_active(dev_t dev);
int lmfs_journal_commit(void);
void lmfs_journal_sync(void);
void lmfs_journal_forget(struct buf *bp);
void lmfs_journal_stop(void);
void lmfs_txn_begin(void);
void lmfs_txn_end(void);

/* Block I/O helper functions. */
void lmfs_driver(dev_t dev, char *label);
ssize_t lmfs_bio(dev_t dev, struct fsdriver_data *data, size_t bytes,
	off_t pos, int call);
void lmfs_bflush(dev_t dev);

#endif /* _MINIX_FSLIB_H */
