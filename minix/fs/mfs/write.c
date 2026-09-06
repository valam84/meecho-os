/* This file is the counterpart of "read.c".  It contains the code for writing
 * insofar as this is not contained in fs_readwrite().
 *
 * The entry points into this file are
 *   write_map:    write a new zone into an inode
 *   clear_zone:   erase a zone in the middle of a file
 *   new_block:    acquire a new block
 *   zero_block:   overwrite a block with zeroes
 *
 */

#include "fs.h"
#include <string.h>
#include <assert.h>
#include <sys/param.h>
#include "buf.h"
#include "inode.h"
#include "super.h"


static void wr_indir(struct buf *bp, int index, zone_t zone);
static int empty_indir(struct buf *, struct super_block *);
static int map_subtree(struct inode *rip, zone_t *rootp, int level,
	u64_t index, zone_t new_zone, int op);


/*===========================================================================*
 *				map_subtree				     *
 *===========================================================================*/
static int map_subtree(struct inode *rip, zone_t *rootp, int level,
	u64_t index, zone_t new_zone, int op)
{
/* Put a block number into, or take one out of, the subtree of indirect
 * blocks rooted at *rootp, which is 'level' levels deep.  At level 0 the
 * root is the block number itself, which is where the recursion ends.
 *
 * Indirect blocks are created on the way down when one is needed, and freed
 * on the way up when the last entry of one goes away.  *rootp is updated in
 * both cases, so the caller - the inode, or the level above - stores the
 * new value without having to know which of the two happened.
 *
 * One function for any number of levels: see map_slot() in read.c.
 */
  struct buf *bp;
  zone_t z, child, old;
  u64_t span;
  int i, r, created = FALSE;

  if (level == 0) {
	if (op & WMAP_FREE) {
		if (*rootp != NO_ZONE) {
			free_zone(rip->i_dev, *rootp);
			*rootp = NO_ZONE;
		}
	} else {
		*rootp = new_zone;
	}
	return(OK);
  }

  z = *rootp;
  if (z == NO_ZONE) {
	if (op & WMAP_FREE)
		return(OK);		/* nothing there to free */

	if ((z = alloc_zone(rip->i_dev, rip->i_zone[0])) == NO_ZONE)
		return(err_code);

	*rootp = z;
	created = TRUE;
  }

  bp = get_block(rip->i_dev, (block_t) z, created ? NO_READ : NORMAL);
  if (created) {
	zero_block(bp);
	MARKDIRTY(bp);		/* an indirect block is metadata */
  }

  span = map_span(rip, level);
  i = (int) (index / span);

  old = child = rd_indir(bp, i);
  r = map_subtree(rip, &child, level - 1, index % span, new_zone, op);

  if (child != old) {
	wr_indir(bp, i, child);
	MARKDIRTY(bp);
  }

  /* Was that the last entry?  Then this block is no longer holding
   * anything, and the level above is told so by *rootp.
   */
  if (r == OK && (op & WMAP_FREE) && empty_indir(bp, rip->i_sp)) {
	put_block(bp);
	free_zone(rip->i_dev, z);
	*rootp = NO_ZONE;
	return(OK);
  }

  /* An indirect block created for a child that could not be allocated
   * would be a block leaked to nobody, so it goes back too.
   */
  if (r != OK && created && empty_indir(bp, rip->i_sp)) {
	put_block(bp);
	free_zone(rip->i_dev, z);
	*rootp = NO_ZONE;
	return(r);
  }

  put_block(bp);

  return(r);
}

/*===========================================================================*
 *				write_map				     *
 *===========================================================================*/
int write_map(rip, position, new_zone, op)
struct inode *rip;		/* pointer to inode to be changed */
off_t position;			/* file address to be mapped */
zone_t new_zone;		/* zone # to be inserted */
int op;				/* special actions */
{
/* Write a new zone into an inode.
 *
 * If op includes WMAP_FREE, free the data zone corresponding to that position
 * in the inode ('new_zone' is ignored then). Also free any indirect block
 * that loses its last entry, at every level of indirection.
 */
  zone_t root;
  int level, slot, r;
  u64_t index;

  IN_MARKDIRTY(rip);

  /* A zone is a block: V3 requires it, V4 has no other notion. */
  assert(rip->i_sp->s_log_zone_size == 0);

  if (map_slot(rip, (u64_t) position / rip->i_sp->s_block_size, &level, &slot,
      &index) != 0)
	return(EFBIG);

  root = rip->i_zone[slot];
  r = map_subtree(rip, &root, level, index, new_zone, op);
  rip->i_zone[slot] = root;

  return(r);
}

/*===========================================================================*
 *				wr_indir				     *
 *===========================================================================*/
static void wr_indir(bp, index, zone)
struct buf *bp;			/* pointer to indirect block */
int index;			/* index into *bp */
zone_t zone;			/* zone to write */
{
/* Given a pointer to an indirect block, write one entry.  Both formats
 * write a 32-bit block number here.
 */

  struct super_block *sp;

  if(bp == NULL)
	panic("wr_indir() on NULL");

  sp = &superblock;

  b_v2_ind(bp)[index] = (zone_t)  conv4(sp->s_native, (long) zone);
}


/*===========================================================================*
 *				empty_indir				     *
 *===========================================================================*/
static int empty_indir(bp, sb)
struct buf *bp;			/* pointer to indirect block */
struct super_block *sb;		/* superblock of device block resides on */
{
/* Return nonzero if the indirect block pointed to by bp contains
 * only NO_ZONE entries.
 */
  unsigned int i;
  for(i = 0; i < sb->s_block_size / sizeof(zone_t); i++)
	if( b_v2_ind(bp)[i] != NO_ZONE)
		return(0);

  return(1);
}


/*===========================================================================*
 *				clear_zone				     *
 *===========================================================================*/
void clear_zone(rip, pos, flag)
register struct inode *rip;	/* inode to clear */
off_t __unused pos;		/* points to block to clear */
int __unused flag;		/* 1 if called by new_block, 0 otherwise */
{
/* Zero a zone, possibly starting in the middle.  The parameter 'pos' gives
 * a byte in the first block to be zeroed.  Clearzone() is called from 
 * fs_readwrite(), truncate_inode(), and new_block().
 */
  int scale;

  /* If the block size and zone size are the same, clear_zone() not needed. */
  scale = rip->i_sp->s_log_zone_size;
  assert(scale == 0);
  return;
}


/*===========================================================================*
 *				new_block				     *
 *===========================================================================*/
struct buf *new_block(rip, position)
register struct inode *rip;	/* pointer to inode */
off_t position;			/* file pointer */
{
/* Acquire a new block and return a pointer to it.  Doing so may require
 * allocating a complete zone, and then returning the initial block.
 * On the other hand, the current zone may still have some unused blocks.
 */
  struct buf *bp;
  block_t b, base_block;
  zone_t z;
  zone_t zone_size;
  int scale, r;

  /* Is another block available in the current zone? */
  if ( (b = read_map(rip, position, 0)) == NO_BLOCK) {
	if (rip->i_zsearch == NO_ZONE) {
		/* First search for this file. Start looking from
		 * the file's first data zone to prevent fragmentation
		 */
		if ( (z = rip->i_zone[0]) == NO_ZONE) {
		 	/* No first zone for file either, let alloc_zone
		 	 * decide. */
			z = (zone_t) rip->i_sp->s_firstdatazone;
		}
	} else {
		/* searched before, start from last find */
		z = rip->i_zsearch;
	}
	if ( (z = alloc_zone(rip->i_dev, z)) == NO_ZONE) return(NULL);
	rip->i_zsearch = z;	/* store for next lookup */
	if ( (r = write_map(rip, position, z, 0)) != OK) {
		free_zone(rip->i_dev, z);
		err_code = r;
		return(NULL);
	}

	/* If we are not writing at EOF, clear the zone, just to be safe. */
	if ( position != rip->i_size) clear_zone(rip, position, 1);
	scale = rip->i_sp->s_log_zone_size;
	base_block = (block_t) z << scale;
	zone_size = (zone_t) rip->i_sp->s_block_size << scale;
	b = base_block + (block_t)((position % zone_size)/rip->i_sp->s_block_size);
  }

  r = lmfs_get_block_ino(&bp, rip->i_dev, b, NO_READ, rip->i_num,
  	rounddown(position, rip->i_sp->s_block_size));
  if (r != OK)
	panic("MFS: error getting block (%llu,%u): %d", rip->i_dev, b, r);
  zero_block(bp);
  return(bp);
}


/*===========================================================================*
 *				zero_block				     *
 *===========================================================================*/
void zero_block(bp)
register struct buf *bp;	/* pointer to buffer to zero */
{
/* Zero a block.  Whether it is data or metadata is the caller's business:
 * a new indirect block is metadata and says so right after this, a new
 * directory block is marked by whoever puts an entry in it, and a new data
 * block is data.
 */
  ASSERT(bp->data);
  memset(b_data(bp), 0, lmfs_fs_block_size());
  MARKDIRTY_DATA(bp);
}

