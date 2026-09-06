#ifndef _MINIX_JOURNAL_H
#define _MINIX_JOURNAL_H

/*
 * The on-disk layout of the metadata journal libminixfs keeps.
 *
 * It is here rather than inside the library because mkfs writes the journal
 * superblock when it creates the journal, and fsck reads it; the three have
 * to agree about it, so it is described once.
 *
 * What the journal is and why it is shaped this way is in
 * port/PORTING-LOG.md, "Этап 7.4".  In short: the journal is a contiguous
 * run of blocks belonging to a reserved inode, holding whole metadata
 * blocks - not descriptions of changes - so that replaying it needs to know
 * nothing about the file system whose blocks they are.
 *
 * Little-endian, like everything else this fork writes.
 */

#include <stdint.h>

/* "MFSJ" in the order the bytes lie on the disk. */
#define JOURNAL_MAGIC		0x4a53464dUL
#define JOURNAL_VERSION		1

/*
 * Block 0 of the journal.  It says whether there is anything to replay, and
 * that is all the state a journal has between mounts.
 */
struct journal_super {
	uint32_t js_magic;
	uint32_t js_version;
	uint32_t js_block_size;
	uint32_t js_nblocks;		/* the whole journal, this block too */
	uint32_t js_start;		/* journal block of the oldest open
					 * transaction; 0 means "nothing to
					 * replay" */
	uint32_t js_sequence;		/* its sequence number */
	uint32_t js_reserved[2];
	uint8_t  js_uuid[16];		/* of the volume, so that a journal
					 * cannot be replayed onto another */
};

/*
 * A transaction is a descriptor block, the blocks themselves, and a commit
 * block.  Descriptor and commit share a header; the descriptor continues
 * with the home block number of each block that follows it, in order.
 */
#define JOURNAL_DESCRIPTOR	1
#define JOURNAL_COMMIT		2

struct journal_head {
	uint32_t jh_magic;
	uint32_t jh_type;		/* JOURNAL_DESCRIPTOR or _COMMIT */
	uint32_t jh_sequence;
	uint32_t jh_count;		/* blocks in this transaction */
	uint32_t jh_checksum;		/* commit block: crc32 of the
					 * descriptor and every block after
					 * it, so that a torn commit does not
					 * pass for a whole one */
	uint32_t jh_reserved[3];
	uint64_t jh_block[1];		/* descriptor: home block numbers */
};

#define JOURNAL_HEAD_SIZE	((size_t) 32)	/* before jh_block[] */

/* How many blocks one descriptor block can name. */
#define JOURNAL_MAX_BLOCKS(bsize) \
	(unsigned int)(((bsize) - JOURNAL_HEAD_SIZE) / sizeof(uint64_t))

/*
 * The smallest journal worth having: a descriptor, a commit and a few
 * blocks between them, plus the journal superblock.
 */
#define JOURNAL_MIN_BLOCKS	16

#endif /* _MINIX_JOURNAL_H */
