/*
 * The metadata journal.
 *
 * A file system asks for a block to be remembered by marking it dirty with
 * lmfs_markdirty_meta() instead of lmfs_markdirty().  Such a block joins the
 * open transaction and is pinned there: it is not written to its place on the
 * disk until the transaction has been written to the journal and the device
 * has been told to make that durable.  After a crash the journal is replayed,
 * which puts every block of the last complete transaction where it belongs,
 * and the file system is whole again without anyone having to check it.
 *
 * What is in the journal is whole blocks, not descriptions of changes, so
 * this code knows nothing about the format of the file system whose blocks
 * it carries.  The design, the ordering rules, and why each of them is
 * necessary are in port/PORTING-LOG.md, "Этап 7.4".
 */

#define _SYSTEM

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <machine/vmparam.h>

#include <sys/mman.h>
#include <sys/param.h>

#include <minix/const.h>
#include <minix/type.h>
#include <minix/syslib.h>
#include <minix/sysutil.h>
#include <minix/u64.h>
#include <minix/bdev.h>
#include <minix/journal.h>
#include <minix/libminixfs.h>

#include "inc.h"

/*
 * The journal of the one file system this process serves.  A file system
 * server has exactly one mounted device, so there is exactly one of these.
 */
static struct {
	int active;			/* is there a journal to write to? */
	int committing;			/* guards against re-entry */
	unsigned int depth;		/* open lmfs_txn_begin() nesting */
	dev_t dev;
	block64_t start;		/* device block of journal block 0 */
	unsigned int nblocks;		/* blocks in the journal */
	size_t bsize;
	uint32_t sequence;		/* of the transaction being built */
	uint8_t uuid[16];

	unsigned int ops;		/* operations since the last commit */
	unsigned int max;		/* blocks one transaction may hold */
	unsigned int n;			/* blocks it holds now */
	struct buf **bp;		/* those blocks, pinned */

	void *scratch;			/* one block, for descriptor and
					 * commit records */
} jrnl;

/*
 * How many operations may go by before the journal is written.  A crash
 * costs the operations since the last commit, so this is how much work is
 * at stake; three device flushes per commit is what it buys.
 */
#define JOURNAL_OPS_PER_COMMIT	16

static int quiet_journal = 0;

/*===========================================================================*
 *				crc32					     *
 *===========================================================================*/
/*
 * The usual CRC-32 (the one Ethernet and zlib use).  It is here to tell a
 * commit record that was written whole from one that was interrupted in the
 * middle: without it, the last few bytes of a journal could be anything and
 * still look like a commit.
 */
static uint32_t crc_table[256];
static int crc_ready = 0;

static void crc32_init(void)
{
	uint32_t c;
	int i, k;

	for (i = 0; i < 256; i++) {
		c = (uint32_t) i;
		for (k = 0; k < 8; k++)
			c = (c & 1) ? (0xedb88320UL ^ (c >> 1)) : (c >> 1);
		crc_table[i] = c;
	}
	crc_ready = 1;
}

static uint32_t crc32_update(uint32_t crc, const void *data, size_t len)
{
	const uint8_t *p = data;

	if (!crc_ready) crc32_init();

	crc ^= 0xffffffffUL;
	while (len-- > 0)
		crc = crc_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);

	return crc ^ 0xffffffffUL;
}

/*===========================================================================*
 *				block buffers				     *
 *===========================================================================*/
/*
 * A buffer the block driver can take in one piece: page-aligned, and
 * allocated the way the cache allocates its blocks.  Memory from malloc()
 * may straddle pages that are not next to each other in physical memory,
 * which a driver mapping it for DMA refuses.
 */
static void *alloc_iobuf(size_t size)
{
	void *p;

	p = mmap(0, size, PROT_READ | PROT_WRITE, MAP_PREALLOC | MAP_ANON,
	    -1, 0);

	return (p == MAP_FAILED) ? NULL : p;
}

static void free_iobuf(void *p, size_t size)
{
	if (p != NULL)
		(void) munmap(p, roundup(size, PAGE_SIZE));
}

/*
 * Fill in an I/O vector for one block, which the driver wants split at page
 * boundaries: a page is contiguous in physical memory and a larger run is
 * not necessarily.
 */
static unsigned int block_iovec(iovec_t *iov, void *data, size_t bsize)
{
	vir_bytes addr = (vir_bytes) data;
	size_t left = bsize;
	unsigned int n = 0;

	while (left > 0) {
		size_t chunk = (left > PAGE_SIZE) ? PAGE_SIZE : left;

		iov[n].iov_addr = addr;
		iov[n].iov_size = chunk;
		addr += chunk;
		left -= chunk;
		n++;
	}

	return n;
}

#define IOVECS_PER_BLOCK(bsize)	((unsigned int) howmany(bsize, PAGE_SIZE))

/*===========================================================================*
 *				journal I/O				     *
 *===========================================================================*/
/*
 * Write 'count' blocks into the journal, starting at journal block 'jblk'.
 * The journal is a contiguous run of device blocks, so this is one sequential
 * write, split only where the driver's vector runs out.
 */
static int journal_write(unsigned int jblk, void **data, unsigned int count)
{
	static iovec_t iov[NR_IOREQS];
	unsigned int done = 0, per;
	ssize_t r;

	per = IOVECS_PER_BLOCK(jrnl.bsize);
	if (per > NR_IOREQS)
		return EINVAL;

	while (done < count) {
		unsigned int nb = 0, niov = 0;
		off_t pos;

		while (done + nb < count && niov + per <= NR_IOREQS) {
			niov += block_iovec(&iov[niov], data[done + nb],
			    jrnl.bsize);
			nb++;
		}

		pos = (off_t) (jrnl.start + jblk + done) * jrnl.bsize;
		r = bdev_scatter(jrnl.dev, pos, iov, niov, BDEV_NOFLAGS);
		if (r != (ssize_t) (nb * jrnl.bsize)) {
			printf("journal: write of %u block(s) at %u failed "
			    "(%zd)\n", nb, jblk + done, r);
			return EIO;
		}

		done += nb;
	}

	return OK;
}

static int journal_read(dev_t dev, block64_t start, size_t bsize,
	unsigned int jblk, void *data)
{
	static iovec_t iov[NR_IOREQS];
	unsigned int niov;
	ssize_t r;

	niov = block_iovec(iov, data, bsize);
	r = bdev_gather(dev, (off_t) (start + jblk) * bsize, iov, niov,
	    BDEV_NOFLAGS);
	if (r != (ssize_t) bsize) {
		printf("journal: read of block %u failed (%zd)\n", jblk, r);
		return EIO;
	}

	return OK;
}

/*
 * Write the journal superblock.  This is the only state a journal keeps
 * between mounts: whether there is a transaction to replay, and which.
 */
static int journal_write_super(dev_t dev, block64_t start, size_t bsize,
	void *scratch, unsigned int nblocks, const uint8_t *uuid,
	uint32_t js_start, uint32_t sequence)
{
	struct journal_super *js = scratch;
	void *data[1];

	memset(js, 0, bsize);
	js->js_magic = JOURNAL_MAGIC;
	js->js_version = JOURNAL_VERSION;
	js->js_block_size = (uint32_t) bsize;
	js->js_nblocks = nblocks;
	js->js_start = js_start;
	js->js_sequence = sequence;
	memcpy(js->js_uuid, uuid, sizeof(js->js_uuid));

	data[0] = scratch;

	if (jrnl.active && dev == jrnl.dev)
		return journal_write(0, data, 1);

	/* Replay writes the superblock before the journal is in use. */
	{
		static iovec_t iov[NR_IOREQS];
		unsigned int niov = block_iovec(iov, scratch, bsize);
		ssize_t r;

		r = bdev_scatter(dev, (off_t) start * bsize, iov, niov,
		    BDEV_NOFLAGS);
		if (r != (ssize_t) bsize)
			return EIO;
	}

	return OK;
}

/*===========================================================================*
 *				lmfs_journal_replay			     *
 *===========================================================================*/
int lmfs_journal_replay(dev_t dev, block64_t start, unsigned int nblocks,
	size_t bsize, const uint8_t *uuid, unsigned int *replayed)
{
/* Put the blocks of the last complete transaction where they belong.  This
 * runs at mount time, before anything has been read from the file system,
 * because until it has run the metadata on the disk may be half of one
 * operation and half of another.
 *
 * Returns OK when the journal was read and whatever it held was replayed -
 * including the common case of it holding nothing.  *replayed is set to the
 * number of blocks written.
 */
	struct journal_super js;
	struct journal_head *jh;
	void *scratch, *blk;
	uint32_t crc, sequence;
	unsigned int count, i;
	block64_t *home = NULL;
	int r;

	*replayed = 0;

	if (nblocks < JOURNAL_MIN_BLOCKS)
		return EINVAL;

	if ((scratch = alloc_iobuf(bsize)) == NULL)
		return ENOMEM;
	if ((blk = alloc_iobuf(bsize)) == NULL) {
		free_iobuf(scratch, bsize);
		return ENOMEM;
	}

	if ((r = journal_read(dev, start, bsize, 0, scratch)) != OK)
		goto out;

	memcpy(&js, scratch, sizeof(js));

	if (js.js_magic != JOURNAL_MAGIC || js.js_version != JOURNAL_VERSION) {
		printf("journal: no journal on this device\n");
		r = EINVAL;
		goto out;
	}
	if (js.js_block_size != bsize || js.js_nblocks != nblocks) {
		printf("journal: journal is %u blocks of %u bytes, the file "
		    "system says %u of %u\n", js.js_nblocks, js.js_block_size,
		    nblocks, (unsigned int) bsize);
		r = EINVAL;
		goto out;
	}
	if (memcmp(js.js_uuid, uuid, sizeof(js.js_uuid)) != 0) {
		printf("journal: this journal belongs to another volume\n");
		r = EINVAL;
		goto out;
	}

	if (js.js_start == 0) {
		r = OK;			/* nothing to replay */
		goto out;
	}

	/*
	 * First pass: read the transaction and check it whole.  A transaction
	 * whose commit record is missing or does not match was interrupted
	 * while being written, and the file system is then in the state it
	 * had before it - which is a state, so there is nothing to do.
	 */
	if (js.js_start >= nblocks) {
		printf("journal: start block %u is outside the journal\n",
		    js.js_start);
		r = EINVAL;
		goto out;
	}

	if ((r = journal_read(dev, start, bsize, js.js_start, scratch)) != OK)
		goto out;

	jh = scratch;
	sequence = js.js_sequence;

	if (jh->jh_magic != JOURNAL_MAGIC ||
	    jh->jh_type != JOURNAL_DESCRIPTOR ||
	    jh->jh_sequence != sequence) {
		if (!quiet_journal)
			printf("journal: no transaction to replay\n");
		goto discard;
	}

	count = jh->jh_count;
	if (count == 0 || count > JOURNAL_MAX_BLOCKS(bsize) ||
	    js.js_start + count + 2 > nblocks) {
		printf("journal: transaction of %u blocks does not fit\n",
		    count);
		goto discard;
	}

	if ((home = malloc(count * sizeof(*home))) == NULL) {
		r = ENOMEM;
		goto out;
	}
	for (i = 0; i < count; i++)
		home[i] = jh->jh_block[i];

	crc = crc32_update(0, scratch, bsize);

	for (i = 0; i < count; i++) {
		if ((r = journal_read(dev, start, bsize, js.js_start + 1 + i,
		    blk)) != OK)
			goto out;
		crc = crc32_update(crc, blk, bsize);
	}

	if ((r = journal_read(dev, start, bsize, js.js_start + 1 + count,
	    scratch)) != OK)
		goto out;

	jh = scratch;
	if (jh->jh_magic != JOURNAL_MAGIC || jh->jh_type != JOURNAL_COMMIT ||
	    jh->jh_sequence != sequence || jh->jh_count != count) {
		printf("journal: transaction %u was not committed; "
		    "nothing to replay\n", sequence);
		goto discard;
	}
	if (jh->jh_checksum != crc) {
		printf("journal: transaction %u is damaged (checksum); "
		    "nothing to replay\n", sequence);
		goto discard;
	}

	/* Second pass: the transaction is whole, so put it where it goes. */
	for (i = 0; i < count; i++) {
		static iovec_t iov[NR_IOREQS];
		unsigned int niov;
		ssize_t w;

		if ((r = journal_read(dev, start, bsize, js.js_start + 1 + i,
		    blk)) != OK)
			goto out;

		niov = block_iovec(iov, blk, bsize);
		w = bdev_scatter(dev, (off_t) home[i] * bsize, iov, niov,
		    BDEV_NOFLAGS);
		if (w != (ssize_t) bsize) {
			printf("journal: replaying block %"PRIu64" failed "
			    "(%zd)\n", home[i], w);
			r = EIO;
			goto out;
		}
	}

	(void) bdev_flush(dev);

	*replayed = count;

	printf("journal: replayed transaction %u, %u block(s)\n", sequence,
	    count);

discard:
	/*
	 * Empty the journal, whether its transaction was replayed or thrown
	 * away.  A transaction that was interrupted while being written is
	 * not a failure and not something to keep: the file system is in the
	 * state it had before it, and the journal has to be usable again for
	 * the mount that follows.  Leaving it there would make every mount
	 * after a crash inside a commit refuse to open the journal at all.
	 */
	r = journal_write_super(dev, start, bsize, scratch, nblocks, uuid, 0,
	    sequence + 1);
	if (r == OK)
		(void) bdev_flush(dev);

out:
	if (home != NULL) free(home);
	free_iobuf(blk, bsize);
	free_iobuf(scratch, bsize);

	return r;
}

/*===========================================================================*
 *				lmfs_journal_init			     *
 *===========================================================================*/
int lmfs_journal_init(dev_t dev, block64_t start, unsigned int nblocks,
	size_t bsize, const uint8_t *uuid)
{
/* Take the journal into use.  It must have been replayed first: this reads
 * the journal superblock and expects it to say that nothing is pending.
 */
	struct journal_super js;
	unsigned int max;
	int r;

	if (jrnl.active)
		return EBUSY;
	if (nblocks < JOURNAL_MIN_BLOCKS)
		return EINVAL;

	memset(&jrnl, 0, sizeof(jrnl));

	if ((jrnl.scratch = alloc_iobuf(bsize)) == NULL)
		return ENOMEM;

	if ((r = journal_read(dev, start, bsize, 0, jrnl.scratch)) != OK) {
		free_iobuf(jrnl.scratch, bsize);
		jrnl.scratch = NULL;
		return r;
	}

	memcpy(&js, jrnl.scratch, sizeof(js));

	if (js.js_magic != JOURNAL_MAGIC || js.js_version != JOURNAL_VERSION ||
	    js.js_block_size != bsize || js.js_nblocks != nblocks ||
	    memcmp(js.js_uuid, uuid, sizeof(js.js_uuid)) != 0) {
		free_iobuf(jrnl.scratch, bsize);
		jrnl.scratch = NULL;
		return EINVAL;
	}

	if (js.js_start != 0) {
		/* Replay should have cleared this. */
		printf("journal: refusing to use a journal that still has "
		    "a transaction in it\n");
		free_iobuf(jrnl.scratch, bsize);
		jrnl.scratch = NULL;
		return EINVAL;
	}

	/*
	 * How many blocks one transaction may hold: what a descriptor block
	 * can name, what fits in the journal beside a descriptor and a commit
	 * record, and no more than a quarter of the journal, so that a
	 * transaction is not the whole of it.
	 */
	max = JOURNAL_MAX_BLOCKS(bsize);
	if (max > nblocks - 3)
		max = nblocks - 3;
	if (max > (nblocks - 1) / 2)
		max = (nblocks - 1) / 2;

	if ((jrnl.bp = malloc(max * sizeof(*jrnl.bp))) == NULL) {
		free_iobuf(jrnl.scratch, bsize);
		jrnl.scratch = NULL;
		return ENOMEM;
	}

	jrnl.dev = dev;
	jrnl.start = start;
	jrnl.nblocks = nblocks;
	jrnl.bsize = bsize;
	jrnl.sequence = js.js_sequence ? js.js_sequence : 1;
	jrnl.max = max;
	jrnl.n = 0;
	memcpy(jrnl.uuid, uuid, sizeof(jrnl.uuid));
	jrnl.active = 1;

	return OK;
}

/*===========================================================================*
 *				lmfs_journal_active			     *
 *===========================================================================*/
int lmfs_journal_active(dev_t dev)
{
	return jrnl.active && jrnl.dev == dev;
}

/*===========================================================================*
 *				lmfs_journal_commit			     *
 *===========================================================================*/
int lmfs_journal_commit(void)
{
/* Write the open transaction to the journal, then put its blocks where they
 * belong.  The order of the steps below, and the flush after each of the
 * first four, is the whole of the guarantee: see port/PORTING-LOG.md.
 */
	struct journal_head *jh;
	void **data;
	uint32_t crc;
	unsigned int i, count;
	int r;

	if (!jrnl.active || jrnl.n == 0)
		return OK;
	if (jrnl.committing)
		return OK;

	jrnl.committing = 1;
	count = jrnl.n;

	/* 1. Data blocks first: after a replay no inode may point at a block
	 * that was never written.  Metadata blocks are pinned, so this writes
	 * data and nothing else.
	 */
	lmfs_flushdev(jrnl.dev);
	(void) bdev_flush(jrnl.dev);

	/* 2. The descriptor and the blocks themselves. */
	if ((data = malloc((count + 1) * sizeof(*data))) == NULL) {
		jrnl.committing = 0;
		return ENOMEM;
	}

	jh = jrnl.scratch;
	memset(jh, 0, jrnl.bsize);
	jh->jh_magic = JOURNAL_MAGIC;
	jh->jh_type = JOURNAL_DESCRIPTOR;
	jh->jh_sequence = jrnl.sequence;
	jh->jh_count = count;
	for (i = 0; i < count; i++)
		jh->jh_block[i] = jrnl.bp[i]->lmfs_blocknr;

	data[0] = jrnl.scratch;
	for (i = 0; i < count; i++)
		data[i + 1] = jrnl.bp[i]->data;

	crc = crc32_update(0, jrnl.scratch, jrnl.bsize);
	for (i = 0; i < count; i++)
		crc = crc32_update(crc, jrnl.bp[i]->data, jrnl.bsize);

	r = journal_write(1, data, count + 1);
	free(data);
	if (r != OK)
		goto fail;

	(void) bdev_flush(jrnl.dev);

	/* 3. The commit record, which says the transaction is whole. */
	jh = jrnl.scratch;
	memset(jh, 0, jrnl.bsize);
	jh->jh_magic = JOURNAL_MAGIC;
	jh->jh_type = JOURNAL_COMMIT;
	jh->jh_sequence = jrnl.sequence;
	jh->jh_count = count;
	jh->jh_checksum = crc;

	{
		void *one[1];

		one[0] = jrnl.scratch;
		if ((r = journal_write(1 + count, one, 1)) != OK)
			goto fail;
	}

	(void) bdev_flush(jrnl.dev);

	/* 4. Now the journal superblock may point at it: from this moment a
	 * crash is repaired rather than rolled back.
	 */
	r = journal_write_super(jrnl.dev, jrnl.start, jrnl.bsize, jrnl.scratch,
	    jrnl.nblocks, jrnl.uuid, 1, jrnl.sequence);
	if (r != OK)
		goto fail;

	(void) bdev_flush(jrnl.dev);

	/* 5. The blocks can go home.  Unpinning them is all it takes: the
	 * cache writes dirty blocks nobody holds, which is what it was
	 * waiting to be allowed to do.
	 */
	for (i = 0; i < count; i++) {
		jrnl.bp[i]->lmfs_journaled = 0;
		lmfs_put_block(jrnl.bp[i]);
	}
	jrnl.n = 0;
	jrnl.ops = 0;

	lmfs_flushdev(jrnl.dev);
	(void) bdev_flush(jrnl.dev);

	/* 6. And the journal is spent.  No flush is needed here: replaying a
	 * transaction whose blocks are already home writes the same bytes to
	 * the same places.
	 */
	jrnl.sequence++;
	(void) journal_write_super(jrnl.dev, jrnl.start, jrnl.bsize,
	    jrnl.scratch, jrnl.nblocks, jrnl.uuid, 0, jrnl.sequence);

	jrnl.committing = 0;

	return OK;

fail:
	/*
	 * The journal could not be written.  The blocks stay pinned and dirty,
	 * so nothing half-done reaches the disk; the file system will try
	 * again at the next commit, and if the device is gone for good the
	 * mount will end with them still in memory - which loses the changes
	 * but does not corrupt what is on the disk.
	 */
	printf("journal: commit failed (%d); the transaction is kept\n", r);
	jrnl.committing = 0;

	return r;
}

/*===========================================================================*
 *				lmfs_txn_begin				     *
 *===========================================================================*/
void lmfs_txn_begin(void)
{
/* An operation that must reach the disk whole starts here.  While one is
 * open the journal is not committed, so that a transaction never holds half
 * of an operation.
 */
	jrnl.depth++;
}

/*===========================================================================*
 *				lmfs_txn_end				     *
 *===========================================================================*/
void lmfs_txn_end(void)
{
	assert(jrnl.depth > 0);

	if (--jrnl.depth > 0)
		return;

	/*
	 * The operation is over, so this is a place where the transaction may
	 * be closed.  Two things close it: it has grown to half of what it
	 * may hold, or a number of operations have gone by since the last
	 * commit.
	 *
	 * The second is what bounds how much work a crash costs.  Committing
	 * after every operation would spend three device flushes on each of
	 * them, and this file system has no thread to commit on a timer the
	 * way ext3 does, so the count stands in for the timer.
	 */
	if (!jrnl.active || jrnl.n == 0)
		return;

	jrnl.ops++;

	if (jrnl.n >= jrnl.max / 2 || jrnl.ops >= JOURNAL_OPS_PER_COMMIT)
		(void) lmfs_journal_commit();
}

/*===========================================================================*
 *				lmfs_markdirty_meta			     *
 *===========================================================================*/
void lmfs_markdirty_meta(struct buf *bp)
{
/* Mark a block dirty and put it in the open transaction: it is metadata, and
 * metadata is what the journal is for.  Without a journal this is exactly
 * lmfs_markdirty(), which is what makes the journal optional.
 */
	struct buf *pin;

	lmfs_markdirty(bp);

	if (!jrnl.active || bp->lmfs_dev != jrnl.dev || bp->lmfs_journaled)
		return;

	/*
	 * A transaction that is full has to be closed even in the middle of an
	 * operation: the alternative is to pin the whole cache.  An operation
	 * split this way can leave blocks allocated but unreferenced after a
	 * crash - a leak, which fsck reclaims - and never an inconsistency.
	 */
	if (jrnl.n == jrnl.max) {
		if (lmfs_journal_commit() != OK)
			return;		/* the block stays merely dirty */
		if (jrnl.n == jrnl.max)
			return;
	}

	/*
	 * Pinning is done by holding the block: a held block is not on the
	 * LRU chain, so it cannot be evicted, and lmfs_flushdev() skips it,
	 * so it cannot be written home before the journal says it may be.
	 */
	if (lmfs_get_block(&pin, bp->lmfs_dev, bp->lmfs_blocknr, NORMAL) != OK)
		return;
	assert(pin == bp);

	bp->lmfs_journaled = 1;
	jrnl.bp[jrnl.n++] = bp;
}

/*===========================================================================*
 *				lmfs_journal_sync			     *
 *===========================================================================*/
void lmfs_journal_sync(void)
{
/* Commit, unless an operation is open.  This is what a sync of the cache
 * calls: it must not close a transaction that holds half of an operation,
 * and there is no such half at the moment a file system asks for a sync.
 */
	if (jrnl.active && jrnl.depth == 0)
		(void) lmfs_journal_commit();
}

/*===========================================================================*
 *				lmfs_journal_forget			     *
 *===========================================================================*/
void lmfs_journal_forget(struct buf *bp)
{
/* The file system has freed this block, so whatever it holds is no longer
 * worth writing anywhere.  Take it out of the transaction; the change that
 * freed it is journaled in its own right.
 */
	unsigned int i;

	if (!bp->lmfs_journaled)
		return;

	for (i = 0; i < jrnl.n; i++) {
		if (jrnl.bp[i] != bp)
			continue;

		jrnl.bp[i] = jrnl.bp[--jrnl.n];
		bp->lmfs_journaled = 0;
		lmfs_put_block(bp);
		return;
	}

	/* Marked but not found: should not happen, and would mean a block
	 * pinned forever, so say so rather than leak quietly.
	 */
	printf("journal: freed block %"PRIu64" was marked but not held\n",
	    bp->lmfs_blocknr);
	bp->lmfs_journaled = 0;
}

/*===========================================================================*
 *				lmfs_journal_stop			     *
 *===========================================================================*/
void lmfs_journal_stop(void)
{
/* The file system is going away: commit what is open and let the journal go.
 */
	if (!jrnl.active)
		return;

	jrnl.depth = 0;
	(void) lmfs_journal_commit();
	(void) bdev_flush(jrnl.dev);

	if (jrnl.n != 0) {
		/* The commit failed; the blocks are still pinned.  Let them
		 * go, so that unmounting can finish.
		 */
		unsigned int i;

		for (i = 0; i < jrnl.n; i++) {
			jrnl.bp[i]->lmfs_journaled = 0;
			lmfs_put_block(jrnl.bp[i]);
		}
		jrnl.n = 0;
	}

	free_iobuf(jrnl.scratch, jrnl.bsize);
	free(jrnl.bp);

	memset(&jrnl, 0, sizeof(jrnl));
}
