#include "fs.h"
#include "inode.h"
#include "super.h"
#include <minix/vfsif.h>
#include <minix/bdev.h>
#include <minix/journal.h>
#include <inttypes.h>

static int journal_start(int readonly, int *journaled);

/*===========================================================================*
 *				journal_start				     *
 *===========================================================================*/
static int journal_start(int readonly, int *journaled)
{
/* Find this file system's journal, replay whatever it holds, and hand it to
 * the block cache to write to from here on.
 *
 * The journal is the blocks of a reserved inode.  Reading that inode before
 * the journal has been replayed looks like trusting metadata that may be
 * half-written - and would be, except that the journal file is made by mkfs
 * and never changed afterwards, so no transaction has ever touched it.
 *
 * The library wants the journal as a first block and a count, not as a file:
 * it must not have to know how this file system stores a block map, since
 * the same journal is meant to serve another one day.  That is why the file
 * has to be contiguous, and mkfs lays it out that way.
 */
  struct inode *rip;
  block64_t start, b;
  unsigned int nblocks, i, replayed;
  int r;

  *journaled = 0;

  if ((rip = get_inode(fs_dev, (ino_t) superblock.s_journal_inum)) == NULL) {
	printf("MFS: journal inode %u is not there\n",
	    superblock.s_journal_inum);
	return EINVAL;
  }

  nblocks = (unsigned int) (rip->i_size / superblock.s_block_size);
  start = 0;
  r = OK;

  if (nblocks < JOURNAL_MIN_BLOCKS) {
	printf("MFS: journal of %u blocks is too small\n", nblocks);
	r = EINVAL;
  }

  for (i = 0; r == OK && i < nblocks; i++) {
	b = read_map(rip, (off_t) i * superblock.s_block_size, 0);
	if (b == NO_BLOCK) {
		printf("MFS: journal has a hole at block %u\n", i);
		r = EINVAL;
	} else if (i == 0) {
		start = b;
	} else if (b != start + i) {
		printf("MFS: journal is not one run of blocks "
		    "(%u is %"PRIu64", not %"PRIu64")\n", i, b, start + i);
		r = EINVAL;
	}
  }

  put_inode(rip);

  if (r != OK)
	return r;

  /*
   * Read-only is not a way to look at a file system that needs its journal
   * replayed: what is on the disk is half of an operation, and replaying is
   * writing.  Say so rather than show whatever is there.
   */
  if (readonly) {
	if (superblock.s_feature_incompat & MFS4_INCOMPAT_RECOVER) {
		printf("MFS: this file system needs its journal replayed, "
		    "which cannot be done read-only\n");
		return EINVAL;
	}
	return OK;
  }

  r = lmfs_journal_replay(fs_dev, start, nblocks, superblock.s_block_size,
	superblock.s_uuid, &replayed);
  if (r != OK) {
	printf("MFS: the journal could not be read (%d)\n", r);
	return r;
  }

  if (replayed > 0) {
	/* Anything read before the replay may be what the replay just
	 * overwrote, so none of it is worth keeping.
	 */
	lmfs_invalidate(fs_dev);

	if ((r = read_super(&superblock)) != OK)
		return r;
  }

  if ((r = lmfs_journal_init(fs_dev, start, nblocks,
      superblock.s_block_size, superblock.s_uuid)) != OK) {
	printf("MFS: the journal could not be started (%d)\n", r);
	return r;
  }

  *journaled = 1;

  return OK;
}

/*===========================================================================*
 *				fs_mount				     *
 *===========================================================================*/
int fs_mount(dev_t dev, unsigned int flags, struct fsdriver_node *root_node,
	unsigned int *res_flags)
{
/* This function reads the superblock of the partition, gets the root inode
 * and sends back the details of them.
 */
  struct inode *root_ip;
  int r, readonly, journaled = 0;

  fs_dev = dev;
  readonly = (flags & REQ_RDONLY) ? 1 : 0;

  /* Open the device the file system lives on. */
  if (bdev_open(fs_dev, readonly ? BDEV_R_BIT : (BDEV_R_BIT|BDEV_W_BIT) ) !=
		OK) {
        return(EINVAL);
  }
  
  /* Fill in the super block. */
  superblock.s_dev = fs_dev;	/* read_super() needs to know which dev */
  r = read_super(&superblock);

  /* Is it recognized as a Minix filesystem? */
  if (r != OK) {
	superblock.s_dev = NO_DEV;
	bdev_close(fs_dev);
	return(r);
  }

  /*
   * The block size has to be the file system's before anything but the
   * superblock is read, and the journal is read before anything else at
   * all: until it has been replayed, what is on the disk may be half of
   * an operation that was interrupted.
   */
  lmfs_set_blocksize(superblock.s_block_size);

  if (superblock.s_version == V4 &&
      (superblock.s_feature_compat & MFS4_COMPAT_HAS_JOURNAL) &&
      superblock.s_journal_inum != 0) {
	if ((r = journal_start(readonly, &journaled)) != OK) {
		superblock.s_dev = NO_DEV;
		bdev_close(fs_dev);
		return(r);
	}
  }

  /* clean check: if rw and not clean, switch to readonly - unless there is
   * a journal, which has just made it clean.  Not having to refuse, check
   * or repair a file system that was interrupted is the whole point of
   * having one.
   */
  if(!(superblock.s_flags & MFSFLAG_CLEAN) && !readonly && !journaled) {
	if(bdev_close(fs_dev) != OK)
		panic("couldn't bdev_close after found unclean FS");
	readonly = 1;

	if (bdev_open(fs_dev, BDEV_R_BIT) != OK) {
		panic("couldn't bdev_open after found unclean FS");
		return(EINVAL);
  	}
	printf("MFS: WARNING: FS 0x%llx unclean, mounting readonly\n", fs_dev);
  }

  /* Compute the current number of used zones, and report it to libminixfs.
   * Note that libminixfs really wants numbers of *blocks*, but this MFS
   * implementation dropped support for differing zone/block sizes a while ago.
   */
  used_zones = superblock.s_zones - count_free_bits(&superblock, ZMAP);

  lmfs_set_blockusage(superblock.s_zones, used_zones);
  
  /* Get the root inode of the mounted file system. */
  if( (root_ip = get_inode(fs_dev, ROOT_INODE)) == NULL)  {
	printf("MFS: couldn't get root inode\n");
	superblock.s_dev = NO_DEV;
	bdev_close(fs_dev);
	return(EINVAL);
  }
  
  if(root_ip->i_mode == 0) {
	printf("%s:%d zero mode for root inode?\n", __FILE__, __LINE__);
	put_inode(root_ip);
	superblock.s_dev = NO_DEV;
	bdev_close(fs_dev);
	return(EINVAL);
  }

  superblock.s_rd_only = readonly;
  
  /* Root inode properties */
  root_node->fn_ino_nr = root_ip->i_num;
  root_node->fn_mode = root_ip->i_mode;
  root_node->fn_size = root_ip->i_size;
  root_node->fn_uid = root_ip->i_uid;
  root_node->fn_gid = root_ip->i_gid;
  root_node->fn_dev = NO_DEV;

  *res_flags = RES_NOFLAGS;

  /* Mark it dirty.  With a journal, also mark it as needing recovery: a
   * file system left this way by a crash carries the bit, and a server
   * that does not understand journals refuses it instead of reading
   * metadata that is half of one operation and half of another.
   */
  if(!superblock.s_rd_only) {
	  superblock.s_flags &= ~MFSFLAG_CLEAN;
	  superblock.s_mount_time = clock_time(NULL);
	  if (journaled)
		superblock.s_feature_incompat |= MFS4_INCOMPAT_RECOVER;
	  if(write_super(&superblock) != OK)
		panic("mounting: couldn't write dirty superblock");
  }

  return(r);
}


/*===========================================================================*
 *				fs_mountpt				     *
 *===========================================================================*/
int fs_mountpt(ino_t ino_nr)
{
/* This function looks up the mount point, it checks the condition whether
 * the partition can be mounted on the inode or not. 
 */
  register struct inode *rip;
  int r = OK;
  mode_t bits;
  
  /* Temporarily open the file. */
  if( (rip = get_inode(fs_dev, ino_nr)) == NULL)
	  return(EINVAL);
  
  if(rip->i_mountpoint) r = EBUSY;

  /* It may not be special. */
  bits = rip->i_mode & I_TYPE;
  if (bits == I_BLOCK_SPECIAL || bits == I_CHAR_SPECIAL) r = ENOTDIR;

  put_inode(rip);

  if(r == OK) rip->i_mountpoint = TRUE;

  return(r);
}


/*===========================================================================*
 *				fs_unmount				     *
 *===========================================================================*/
void fs_unmount(void)
{
/* Unmount a file system. */
  int count;
  struct inode *rip, *root_ip;

  /* See if the mounted device is busy.  Only 1 inode using it should be
   * open --the root inode-- and that inode only 1 time.  This is an integrity
   * check only: VFS expects the unmount to succeed either way.
   */
  count = 0;
  for (rip = &inode[0]; rip < &inode[NR_INODES]; rip++) 
	  if (rip->i_count > 0 && rip->i_dev == fs_dev) count += rip->i_count;
  if (count != 1)
	printf("MFS: file system has %d in-use inodes!\n", count);

  if ((root_ip = find_inode(fs_dev, ROOT_INODE)) == NULL)
	panic("MFS: couldn't find root inode\n");
   
  put_inode(root_ip);

  /* force any cached blocks out of memory */
  fs_sync();

  /* Commit and let go of the journal: after this the file system is whole
   * on the disk, and there is nothing to replay.
   */
  lmfs_journal_stop();

  /* Mark it clean if we're allowed to write _and_ it was clean originally. */
  if (!superblock.s_rd_only) {
	superblock.s_flags |= MFSFLAG_CLEAN;
	superblock.s_feature_incompat &= ~(u32_t) MFS4_INCOMPAT_RECOVER;
	write_super(&superblock);
  }

  /* Close the device the file system lives on. */
  bdev_close(fs_dev);

  /* Throw out blocks out of the VM cache, to prevent corruption later. */
  lmfs_invalidate(fs_dev);

  /* Finish off the unmount. */
  superblock.s_dev = NO_DEV;
}

