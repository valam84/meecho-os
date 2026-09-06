/* This file contains the procedures that look up path names in the directory
 * system and determine the inode number that goes with a given path name.
 */
 
#include "fs.h"
#include <assert.h>
#include <string.h>
#include "buf.h"
#include "inode.h"
#include "super.h"

static int search_dir_v3(struct inode *ldir_ptr, const char *string,
	ino_t *numb, int flag);
static int search_dir_v4(struct inode *ldir_ptr, const char *string,
	ino_t *numb, int flag, unsigned type);


/*===========================================================================*
 *                             fs_lookup				     *
 *===========================================================================*/
int fs_lookup(ino_t dir_nr, char *name, struct fsdriver_node *node,
	int *is_mountpt)
{
  struct inode *dirp, *rip;

  /* Find the starting inode. */
  if ((dirp = find_inode(fs_dev, dir_nr)) == NULL)
	return EINVAL;

  /* Look up the directory entry. */
  if ((rip = advance(dirp, name)) == NULL)
	return err_code;

  /* On success, leave the resulting inode open and return its details. */
  node->fn_ino_nr = rip->i_num;
  node->fn_mode = rip->i_mode;
  node->fn_size = rip->i_size;
  node->fn_uid = rip->i_uid;
  node->fn_gid = rip->i_gid;
  /* This is only valid for block and character specials. But it doesn't
   * cause any harm to always set the device field. */
  node->fn_dev = (dev_t) rip->i_zone[0];

  *is_mountpt = rip->i_mountpoint;

  return OK;
}


/*===========================================================================*
 *				advance					     *
 *===========================================================================*/
struct inode *advance(dirp, string)
struct inode *dirp;		/* inode for directory to be searched */
const char *string;		/* component name to look for */
{
/* Given a directory and a component of a path, look up the component in
 * the directory, find the inode, open it, and return a pointer to its inode
 * slot.
 */
  ino_t numb;
  struct inode *rip;

  assert(dirp != NULL);

  /* If 'string' is empty, return an error. */
  if (string[0] == '\0') {
  	err_code = ENOENT;
	return(NULL);
  }

  /* If dir has been removed return ENOENT. */
  if (dirp->i_nlinks == NO_LINK) {
	err_code = ENOENT;
	return(NULL);
  }

  /* If 'string' is not present in the directory, signal error. */
  if ( (err_code = search_dir(dirp, string, &numb, LOOK_UP, 0)) != OK) {
	return(NULL);
  }

  /* The component has been found in the directory.  Get inode. */
  if ( (rip = get_inode(dirp->i_dev, (int) numb)) == NULL)  {
	assert(err_code != OK);
	return(NULL);
  }

  assert(err_code == OK);
  return(rip);
}


/*===========================================================================*
 *				search_dir_v3				     *
 *===========================================================================*/
static int search_dir_v3(ldir_ptr, string, numb, flag)
register struct inode *ldir_ptr; /* ptr to inode for dir to search */
const char *string;		 /* component to search for */
ino_t *numb;			 /* pointer to inode number */
int flag;			 /* LOOK_UP, ENTER, DELETE or IS_EMPTY */
{
/* Search a V3 directory, whose entries are 64 bytes each with the name in
 * the last 60 of them.
 *
 * This function, and this function alone, implements name truncation for
 * V3, by simply considering only the first MFS3_DIRSIZ bytes from 'string'.
 */
  register struct mfs3_dirent *dp = NULL;
  register struct buf *bp = NULL;
  int i, r, e_hit, t, match;
  off_t pos;
  unsigned new_slots, old_slots;
  struct super_block *sp;
  int extended = 0;

  /* Step through the directory one block at a time. */
  old_slots = (unsigned) (ldir_ptr->i_size/DIR_ENTRY_SIZE);
  new_slots = 0;
  e_hit = FALSE;
  match = 0;			/* set when a string match occurs */

  pos = 0;
  if (flag == ENTER && ldir_ptr->i_last_dpos < ldir_ptr->i_size) {
	pos = ldir_ptr->i_last_dpos;
	new_slots = (unsigned) (pos/DIR_ENTRY_SIZE);
  }

  for (; pos < ldir_ptr->i_size; pos += ldir_ptr->i_sp->s_block_size) {
	assert(ldir_ptr->i_dev != NO_DEV);

	/* Since directories don't have holes, 'b' cannot be NO_BLOCK. */
	bp = get_block_map(ldir_ptr, pos);

	assert(ldir_ptr->i_dev != NO_DEV);
	assert(bp != NULL);

	/* Search a directory block. */
	for (dp = &b_dir(bp)[0];
		dp < &b_dir(bp)[NR_DIR_ENTRIES(ldir_ptr->i_sp->s_block_size)];
		dp++) {
		if (++new_slots > old_slots) { /* not found, but room left */
			if (flag == ENTER) e_hit = TRUE;
			break;
		}

		/* Match occurs if string found. */
		if (flag != ENTER && dp->mfs_d_ino != NO_ENTRY) {
			if (flag == IS_EMPTY) {
				/* If this test succeeds, dir is not empty. */
				if (strcmp(dp->mfs_d_name, "." ) != 0 &&
				    strcmp(dp->mfs_d_name, "..") != 0)
					match = 1;
			} else {
				if (strncmp(dp->mfs_d_name, string,
					sizeof(dp->mfs_d_name)) == 0){
					match = 1;
				}
			}
		}

		if (match) {
			/* LOOK_UP or DELETE found what it wanted. */
			r = OK;
			if (flag == IS_EMPTY) r = ENOTEMPTY;
			else if (flag == DELETE) {
				/* Save d_ino for recovery. */
				t = MFS3_DIRSIZ - sizeof(ino_t);
				*((ino_t *) &dp->mfs_d_name[t]) = dp->mfs_d_ino;
				dp->mfs_d_ino = NO_ENTRY; /* erase entry */
				MARKDIRTY(bp);
				ldir_ptr->i_update |= CTIME | MTIME;
				IN_MARKDIRTY(ldir_ptr);
				if (pos < ldir_ptr->i_last_dpos)
					ldir_ptr->i_last_dpos = pos;
			} else {
				sp = ldir_ptr->i_sp;	/* 'flag' is LOOK_UP */
				*numb = (ino_t) conv4(sp->s_native,
						      (int) dp->mfs_d_ino);
			}
			put_block(bp);
			return(r);
		}

		/* Check for free slot for the benefit of ENTER. */
		if (flag == ENTER && dp->mfs_d_ino == 0) {
			e_hit = TRUE;	/* we found a free slot */
			break;
		}
	}

	/* The whole block has been searched or ENTER has a free slot. */
	if (e_hit) break;	/* e_hit set if ENTER can be performed now */
	put_block(bp);		/* otherwise, continue searching dir */
  }

  /* The whole directory has now been searched. */
  if (flag != ENTER) {
  	return(flag == IS_EMPTY ? OK : ENOENT);
  }

  /* When ENTER next time, start searching for free slot from
   * i_last_dpos. It gives some performance improvement (3-5%).
   */
  ldir_ptr->i_last_dpos = pos;

  /* This call is for ENTER.  If no free slot has been found so far, try to
   * extend directory.
   */
  if (e_hit == FALSE) { /* directory is full and no room left in last block */
	new_slots++;		/* increase directory size by 1 entry */
	if (new_slots == 0) return(EFBIG); /* dir size limited by slot count */
	if ( (bp = new_block(ldir_ptr, ldir_ptr->i_size)) == NULL)
		return(err_code);
	dp = &b_dir(bp)[0];
	extended = 1;
  }

  /* 'bp' now points to a directory block with space. 'dp' points to slot. */
  (void) memset(dp->mfs_d_name, 0, (size_t) MFS3_DIRSIZ); /* clear entry */
  for (i = 0; i < MFS3_DIRSIZ && string[i]; i++) dp->mfs_d_name[i] = string[i];
  sp = ldir_ptr->i_sp;
  dp->mfs_d_ino = conv4(sp->s_native, (int) *numb);
  MARKDIRTY(bp);
  put_block(bp);
  ldir_ptr->i_update |= CTIME | MTIME;	/* mark mtime for update later */
  IN_MARKDIRTY(ldir_ptr);
  if (new_slots > old_slots) {
	ldir_ptr->i_size = (off_t) new_slots * DIR_ENTRY_SIZE;
	/* Send the change to disk if the directory is extended. */
	if (extended) rw_inode(ldir_ptr, WRITING);
  }
  return(OK);
}

/*===========================================================================*
 *				dirent_ok				     *
 *===========================================================================*/
int mfs4_dirent_ok(const struct mfs4_dirent *dp, unsigned off,
	unsigned block_size)
{
/* Is this a record the walk can step over?  A directory whose record
 * lengths do not lead from the start of a block to its end exactly is
 * corrupt, and walking it further would be walking over whatever lies in
 * the block.  Every loop below checks each record before using it.
 */
  unsigned len = dp->d_rec_len;

  if (len < MFS4_DIRENT_HDR || len % MFS4_DIRENT_ALIGN != 0)
	return FALSE;
  if (off + len > block_size)
	return FALSE;
  if (dp->d_ino != 0 &&
      (dp->d_name_len == 0 ||
       MFS4_DIRENT_HDR + dp->d_name_len > len))
	return FALSE;

  return TRUE;
}

/*===========================================================================*
 *				dirent_place				     *
 *===========================================================================*/
static void dirent_place(struct mfs4_dirent *dp, ino_t numb,
	const char *string, size_t namelen, unsigned type, size_t need)
{
/* Put a name into a record that has room for it.  A record in use is split:
 * it keeps what its own name needs, and the new record takes the rest.  A
 * free record is used whole, so that no gap too small for any name is left
 * behind.
 */
  struct mfs4_dirent *new;

  if (dp->d_ino != 0) {
	unsigned used = MFS4_DIRENT_LEN(dp->d_name_len);

	new = (struct mfs4_dirent *) ((char *) dp + used);
	new->d_rec_len = dp->d_rec_len - used;
	dp->d_rec_len = used;
	dp = new;
  }

  dp->d_ino = (uint32_t) numb;
  dp->d_name_len = (uint8_t) namelen;
  dp->d_type = (uint8_t) type;
  memcpy(dp->d_name, string, namelen);
  /* Bytes between the name and the end of the record are nobody's; zero
   * them so that what is on the disk does not depend on what was there.
   */
  memset(dp->d_name + namelen, 0, dp->d_rec_len - MFS4_DIRENT_HDR - namelen);

  (void) need;
}

/*===========================================================================*
 *				search_dir_v4				     *
 *===========================================================================*/
static int search_dir_v4(struct inode *ldir_ptr, const char *string,
	ino_t *numb, int flag, unsigned type)
{
/* Search a V4 directory, whose entries are variable-length records chained
 * by their d_rec_len through each block.  A directory is a whole number of
 * blocks, and every block is a chain of records from its first byte to its
 * last, free ones included: there is no count of entries and no "slots".
 */
  struct buf *bp;
  struct mfs4_dirent *dp, *prev;
  struct super_block *sp = ldir_ptr->i_sp;
  unsigned block_size = sp->s_block_size;
  size_t namelen = 0, need = 0;
  off_t pos, start;
  unsigned off;
  int r;

  if (flag != IS_EMPTY) {
	namelen = strlen(string);
	if (namelen == 0)
		return(ENOENT);
	if (namelen > MFS4_NAME_MAX)
		return(flag == ENTER ? ENAMETOOLONG : ENOENT);
	need = MFS4_DIRENT_LEN(namelen);
  }

  start = 0;
  if (flag == ENTER && ldir_ptr->i_last_dpos < ldir_ptr->i_size)
	start = ldir_ptr->i_last_dpos - ldir_ptr->i_last_dpos % block_size;

  for (pos = start; pos < ldir_ptr->i_size; pos += block_size) {
	assert(ldir_ptr->i_dev != NO_DEV);

	/* Since directories don't have holes, this cannot be NULL. */
	bp = get_block_map(ldir_ptr, pos);
	assert(bp != NULL);

	prev = NULL;
	for (off = 0; off + MFS4_DIRENT_HDR <= block_size;
	     off += dp->d_rec_len) {
		dp = (struct mfs4_dirent *) (b_data(bp) + off);

		if (!mfs4_dirent_ok(dp, off, block_size)) {
			printf("MFS: corrupt directory entry in inode %llu "
			    "at %llu+%u\n", ldir_ptr->i_num, pos, off);
			put_block(bp);
			return(EIO);
		}

		if (dp->d_ino == 0) {
			/* A free record: of interest to ENTER only. */
			if (flag == ENTER && dp->d_rec_len >= need) {
				dirent_place(dp, *numb, string, namelen, type,
				    need);
				goto entered;
			}
			continue;
		}

		if (flag == ENTER) {
			/* A record in use with room to spare can be split. */
			if (dp->d_rec_len - MFS4_DIRENT_LEN(dp->d_name_len) >=
			    need) {
				dirent_place(dp, *numb, string, namelen, type,
				    need);
				goto entered;
			}
			continue;
		}

		if (flag == IS_EMPTY) {
			/* Anything but "." and ".." means not empty. */
			if (dp->d_name_len == 1 && dp->d_name[0] == '.')
				continue;
			if (dp->d_name_len == 2 && dp->d_name[0] == '.' &&
			    dp->d_name[1] == '.')
				continue;
			put_block(bp);
			return(ENOTEMPTY);
		}

		if (dp->d_name_len != namelen ||
		    memcmp(dp->d_name, string, namelen) != 0)
			continue;

		/* LOOK_UP or DELETE found what it wanted. */
		if (flag == LOOK_UP) {
			*numb = (ino_t) dp->d_ino;
			put_block(bp);
			return(OK);
		}

		/* DELETE: give the space to the record before this one, so
		 * that a directory does not fill up with unusable gaps.  The
		 * first record of a block has no predecessor and is simply
		 * marked free.
		 */
		if (prev != NULL)
			prev->d_rec_len += dp->d_rec_len;
		else
			dp->d_ino = 0;

		MARKDIRTY(bp);
		put_block(bp);
		ldir_ptr->i_update |= CTIME | MTIME;
		IN_MARKDIRTY(ldir_ptr);
		if (pos < ldir_ptr->i_last_dpos)
			ldir_ptr->i_last_dpos = pos;
		return(OK);
	}

	put_block(bp);
  }

  if (flag != ENTER)
	return(flag == IS_EMPTY ? OK : ENOENT);

  /* No room anywhere: the directory grows by one block, which starts life
   * as a single free record covering all of it.
   */
  if ((bp = new_block(ldir_ptr, ldir_ptr->i_size)) == NULL)
	return(err_code);

  dp = (struct mfs4_dirent *) b_data(bp);
  memset(dp, 0, MFS4_DIRENT_HDR);
  dp->d_rec_len = block_size;
  dirent_place(dp, *numb, string, namelen, type, need);

  ldir_ptr->i_size += block_size;
  ldir_ptr->i_last_dpos = ldir_ptr->i_size - block_size;
  MARKDIRTY(bp);
  put_block(bp);
  ldir_ptr->i_update |= CTIME | MTIME;
  IN_MARKDIRTY(ldir_ptr);
  /* Send the change to disk, as the V3 path does when it extends. */
  rw_inode(ldir_ptr, WRITING);

  return(OK);

entered:
  MARKDIRTY(bp);
  put_block(bp);
  ldir_ptr->i_last_dpos = pos;
  ldir_ptr->i_update |= CTIME | MTIME;
  IN_MARKDIRTY(ldir_ptr);
  r = OK;

  return(r);
}

/*===========================================================================*
 *				search_dir				     *
 *===========================================================================*/
int search_dir(ldir_ptr, string, numb, flag, type)
register struct inode *ldir_ptr; /* ptr to inode for dir to search */
const char *string;		 /* component to search for */
ino_t *numb;			 /* pointer to inode number */
int flag;			 /* LOOK_UP, ENTER, DELETE or IS_EMPTY */
unsigned type;			 /* DT_* of the entry, for ENTER on V4 */
{
/* Look up, enter, delete a name, or ask whether a directory is empty.
 *
 * The two formats keep their entries differently enough that they get a
 * function each rather than a version test per loop; what they share is
 * this door, the checks in front of it, and the meaning of 'flag'.
 *
 * 'type' is the DT_* of the entry being entered, which V4 stores so that
 * readdir does not have to read an inode per name.  V3 has nowhere to put
 * it and ignores it.
 */

  /* If 'ldir_ptr' is not a pointer to a dir inode, error. */
  if ((ldir_ptr->i_mode & I_TYPE) != I_DIRECTORY)
	return(ENOTDIR);

  if ((flag == DELETE || flag == ENTER) && ldir_ptr->i_sp->s_rd_only)
	return(EROFS);

  if (ldir_ptr->i_sp->s_version == V4)
	return search_dir_v4(ldir_ptr, string, numb, flag, type);

  return search_dir_v3(ldir_ptr, string, numb, flag);
}
