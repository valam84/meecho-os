#include "fs.h"
#include "inode.h"
#include <sys/time.h>
#include <sys/stat.h>


/*===========================================================================*
 *				fs_utime				     *
 *===========================================================================*/
int fs_utime(ino_t ino_nr, struct timespec *atime, struct timespec *mtime)
{
  register struct inode *rip;

  /* Temporarily open the file. */
  if( (rip = get_inode(fs_dev, ino_nr)) == NULL)
        return(EINVAL);

  rip->i_update = CTIME; /* discard any stale ATIME and MTIME flags */

  switch (atime->tv_nsec) {
  case UTIME_NOW:
	rip->i_update |= ATIME;
	break;
  case UTIME_OMIT: /* do not touch */
	break;
  default:
	/* V3 has no subsecond resolution, and rounds down; V4 has, and
	 * keeps what it is given.  Setting the field either way costs
	 * nothing: it is dropped on the way to a V3 disk.
	 */
	rip->i_atime = atime->tv_sec;
	rip->i_atime_nsec = atime->tv_nsec;
	break;
  }

  switch (mtime->tv_nsec) {
  case UTIME_NOW:
	rip->i_update |= MTIME;
	break;
  case UTIME_OMIT: /* do not touch */
	break;
  default:
	rip->i_mtime = mtime->tv_sec;
	rip->i_mtime_nsec = mtime->tv_nsec;
	break;
  }

  IN_MARKDIRTY(rip);

  put_inode(rip);
  return(OK);
}

