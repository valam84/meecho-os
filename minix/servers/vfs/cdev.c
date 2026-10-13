/*
 * This file contains routines to perform character device operations.
 * Character drivers may suspend I/O requests on their devices (read, write,
 * ioctl), as well as select requests.  These requests will therefore suspend
 * their calling process, freeing up the associated VFS worker thread for other
 * tasks.  The I/O requests may later be cancelled as a result of the suspended
 * process receiving a signal (which it either catches or dies from), in which
 * case there will be a worker thread associated with the cancellation.  Open
 * and close requests may not suspend and will thus block the calling thread.
 *
 * The entry points in this file are:
 *   cdev_map:    map a character device to its actual device number
 *   cdev_open:   open a character device
 *   cdev_close:  close a character device
 *   cdev_io:     initiate a read, write, or ioctl to a character device
 *   cdev_select: initiate a select call on a device
 *   cdev_cancel: cancel an I/O request, blocking until it has been cancelled
 *   cdev_reply:  process the result of a character driver request
 *   do_cdevfd:   a driver's descriptors of its own device, for a process
 *                waiting on it (the cdevfd(2) back-call)
 */

#include "fs.h"
#include "vnode.h"
#include "file.h"
#include "vmnt.h"
#include <string.h>
#include <fcntl.h>
#include <sys/ttycom.h>
#include <assert.h>

/*
 * Map the given device number to a real device number, remapping /dev/tty to
 * the given process's controlling terminal if it has one.  Perform a bounds
 * check on the resulting device's major number, and return NO_DEV on failure.
 * This function is idempotent but not used that way.
 */
dev_t
cdev_map(dev_t dev, struct fproc * rfp)
{
	devmajor_t major;

	/*
	 * First cover one special case: /dev/tty, the magic device that
	 * translates to the controlling TTY.
	 */
	if ((major = major(dev)) == CTTY_MAJOR) {
		/* No controlling terminal?  Fail the request. */
		if (rfp->fp_tty == NO_DEV) return NO_DEV;

		/* Substitute the controlling terminal device. */
		dev = rfp->fp_tty;
		major = major(dev);
	}

	if (major < 0 || major >= NR_DEVICES) return NO_DEV;

	return dev;
}

/*
 * Obtain the dmap structure for the given device, if a valid driver exists for
 * the major device.  Perform redirection for CTTY_MAJOR.
 */
static struct dmap *
cdev_get(dev_t dev, devminor_t * minor_dev)
{
	struct dmap *dp;
	int slot;

	/*
	 * Remap /dev/tty as needed.  Perform a bounds check on the major
	 * number.
	 */
	if ((dev = cdev_map(dev, fp)) == NO_DEV)
		return NULL;

	/* Determine the driver endpoint. */
	dp = &dmap[major(dev)];

	/* See if driver is roughly valid. */
	if (dp->dmap_driver == NONE) return NULL;

	if (isokendpt(dp->dmap_driver, &slot) != OK) {
		printf("VFS: cdev_get: old driver for major %x (%d)\n",
		    major(dev), dp->dmap_driver);
		return NULL;
	}

	/* Also return the (possibly redirected) minor number. */
	*minor_dev = minor(dev);
	return dp;
}

/*
 * A new minor device number has been returned.  Request PFS to create a
 * temporary device file to hold it.
 */
static int
cdev_clone(int fd, dev_t dev, devminor_t new_minor)
{
	struct vnode *vp;
	struct node_details res;
	int r;

	assert(fd != -1);

	/* Device number of the new device. */
	dev = makedev(major(dev), new_minor);

	/* Create a new file system node on PFS for the cloned device. */
	r = req_newnode(PFS_PROC_NR, fp->fp_effuid, fp->fp_effgid,
	    RWX_MODES | I_CHAR_SPECIAL, dev, &res);
	if (r != OK) {
		(void)cdev_close(dev);
		return r;
	}

	/* Drop the old node and use the new values. */
	if ((vp = get_free_vnode()) == NULL) {
		req_putnode(PFS_PROC_NR, res.inode_nr, 1); /* is this right? */
		(void)cdev_close(dev);
		return err_code;
	}
	lock_vnode(vp, VNODE_OPCL);

	assert(fp->fp_filp[fd] != NULL);
	unlock_vnode(fp->fp_filp[fd]->filp_vno);
	put_vnode(fp->fp_filp[fd]->filp_vno);

	vp->v_fs_e = res.fs_e;
	vp->v_vmnt = NULL;
	vp->v_dev = NO_DEV;
	vp->v_inode_nr = res.inode_nr;
	vp->v_mode = res.fmode;
	vp->v_sdev = dev;
	vp->v_fs_count = 1;
	vp->v_ref_count = 1;
	fp->fp_filp[fd]->filp_vno = vp;

	return OK;
}

/*
 * Open or close a character device.  The given operation must be either
 * CDEV_OPEN or CDEV_CLOSE.  For CDEV_OPEN, 'fd' must be the file descriptor
 * for the file being opened; for CDEV_CLOSE, it is ignored.  For CDEV_OPEN,
 * 'flags' identifies a bitwise combination of R_BIT, W_BIT, and/or O_NOCTTY;
 * for CDEV_CLOSE, it too is ignored.
 */
static int
cdev_opcl(int op, dev_t dev, int fd, int flags)
{
	devminor_t minor_dev, new_minor;
	struct dmap *dp;
	struct fproc *rfp;
	message dev_mess;
	int r, r2, acc;

	/*
	 * We need the a descriptor for CDEV_OPEN, because if the driver
	 * returns a cloned device, we need to replace what the fd points to.
	 * For CDEV_CLOSE however, we may be closing a device for which the
	 * calling process has no file descriptor, and thus we expect no
	 * meaningful fd value in that case.
	 */
	assert(op == CDEV_OPEN || op == CDEV_CLOSE);
	assert(fd != -1 || op == CDEV_CLOSE);

	/* Determine task dmap. */
	if ((dp = cdev_get(dev, &minor_dev)) == NULL)
		return ENXIO;

	/*
	 * CTTY exception: do not actually send the open/close request for
	 * /dev/tty to the driver.  This avoids the case that the actual device
	 * will remain open forever if the process calls setsid() after opening
	 * /dev/tty.
	 */
	if (major(dev) == CTTY_MAJOR) return OK;

	/*
	 * Add O_NOCTTY to the access flags if this process is not a session
	 * leader, or if it already has a controlling tty, or if it is someone
	 * else's controlling tty.  For performance reasons, only search the
	 * full process table if this driver has set controlling TTYs before.
	 */
	if (!(fp->fp_flags & FP_SESLDR) || fp->fp_tty != 0) {
		flags |= O_NOCTTY;
	} else if (!(flags & O_NOCTTY) && dp->dmap_seen_tty) {
		for (rfp = &fproc[0]; rfp < &fproc[NR_PROCS]; rfp++)
			if (rfp->fp_pid != PID_FREE && rfp->fp_tty == dev)
				flags |= O_NOCTTY;
	}

	/* Prepare the request message. */
	memset(&dev_mess, 0, sizeof(dev_mess));
	dev_mess.m_type = op;
	dev_mess.m_vfs_lchardriver_openclose.minor = minor_dev;
	dev_mess.m_vfs_lchardriver_openclose.id = who_e;
	if (op == CDEV_OPEN) {
		acc = 0;
		if (flags & R_BIT) acc |= CDEV_R_BIT;
		if (flags & W_BIT) acc |= CDEV_W_BIT;
		if (flags & O_NOCTTY) acc |= CDEV_NOCTTY;
		dev_mess.m_vfs_lchardriver_openclose.user = who_e;
		dev_mess.m_vfs_lchardriver_openclose.access = acc;
	}

	/* Send the request to the driver. */
	/*
	 * Here and below: asynsend3() fails when its table is full, which a
	 * driver that stops receiving (hung, not dead) is enough to cause.
	 * That used to be a panic; it fails this request instead.
	 */
	if ((r = asynsend3(dp->dmap_driver, &dev_mess, AMF_NOREPLY)) != OK) {
		printf("VFS: asynsend in cdev_opcl failed: %d\n", r);
		return EIO;
	}

	/* Block the thread waiting for a reply. */
	self->w_task = dp->dmap_driver;
	self->w_drv_sendrec = &dev_mess;

	worker_wait();

	self->w_task = NONE;
	assert(self->w_drv_sendrec == NULL);

	/* Process the reply. */
	r = dev_mess.m_lchardriver_vfs_reply.status;

	if (op == CDEV_OPEN && r >= 0) {
		/*
		 * Some devices need special processing upon open.  Such a
		 * device is "cloned", i.e., on a succesful open it is replaced
		 * by a new device with a new unique minor device number.  This
		 * new device number identifies a new object that has been
		 * allocated within a driver.
		 */
		if (r & CDEV_CLONED) {
			new_minor = r & ~(CDEV_CLONED | CDEV_CTTY);
			if ((r2 = cdev_clone(fd, dev, new_minor)) < 0)
				return r2;
		}

		/* Did this call make the TTY the controlling TTY? */
		if (r & CDEV_CTTY) {
			fp->fp_tty = dev;
			dp->dmap_seen_tty = TRUE;
		}

		r = OK;
	}

	/* Return the result from the driver. */
	return r;
}

/*
 * Open a character device.
 */
int
cdev_open(int fd, dev_t dev, int flags)
{

	return cdev_opcl(CDEV_OPEN, dev, fd, flags);
}

/*
 * Close a character device.
 */
int
cdev_close(dev_t dev)
{

	return cdev_opcl(CDEV_CLOSE, dev, -1, 0);
}

/*
 * Initiate a read, write, or ioctl to a character device.  The given operation
 * must be CDEV_READ, CDEV_WRITE, or CDEV_IOCTL.  The call is made on behalf of
 * user process 'proc_e'.  For read/write requests, 'bytes' is the number of
 * bytes to read into 'buf' at file position 'pos'.  For ioctl requests,
 * 'bytes' is actually an IOCTL request code, which implies the size of the
 * buffer 'buf' if needed for the request at all ('pos' is ignored here).  The
 * 'flags' field contains file pointer flags, from which O_NONBLOCK is tested.
 */
int
cdev_io(int op, dev_t dev, endpoint_t proc_e, vir_bytes buf, off_t pos,
	unsigned long bytes, int flags)
{
	devminor_t minor_dev;
	struct dmap *dp;
	message dev_mess;
	cp_grant_id_t gid;
	int r;

	assert(op == CDEV_READ || op == CDEV_WRITE || op == CDEV_IOCTL);

	/* Determine task map. */
	if ((dp = cdev_get(dev, &minor_dev)) == NULL)
		return EIO;

	/*
	 * Handle TIOCSCTTY ioctl: set controlling TTY.  FIXME: this should not
	 * hardcode major device numbers, and not assume that the IOCTL request
	 * succeeds!
	 */
	if (op == CDEV_IOCTL && bytes == TIOCSCTTY &&
	    (major(dev) == TTY_MAJOR || major(dev) == PTY_MAJOR)) {
		fp->fp_tty = dev;
	}

	/* Create a grant for the buffer provided by the user process. */
	if (op != CDEV_IOCTL) {
		gid = cpf_grant_magic(dp->dmap_driver, proc_e, buf,
		    (size_t)bytes, (op == CDEV_READ) ? CPF_WRITE : CPF_READ);
	} else
		gid = make_ioctl_grant(dp->dmap_driver, proc_e, buf, bytes);
	/* No grant could be made (VFS's heap, or a leak): was a panic. */
	if (!GRANT_VALID(gid))
		return ENOMEM;

	/* Set up the message that will be sent to the driver. */
	memset(&dev_mess, 0, sizeof(dev_mess));
	dev_mess.m_type = op;
	dev_mess.m_vfs_lchardriver_readwrite.minor = minor_dev;
	if (op == CDEV_IOCTL) {
		dev_mess.m_vfs_lchardriver_readwrite.request = bytes;
		dev_mess.m_vfs_lchardriver_readwrite.user = proc_e;
	} else {
		dev_mess.m_vfs_lchardriver_readwrite.pos = pos;
		dev_mess.m_vfs_lchardriver_readwrite.count = bytes;
	}
	dev_mess.m_vfs_lchardriver_readwrite.id = proc_e;
	dev_mess.m_vfs_lchardriver_readwrite.grant = gid;
	dev_mess.m_vfs_lchardriver_readwrite.flags = 0;
	if (flags & O_NONBLOCK)
		  dev_mess.m_vfs_lchardriver_readwrite.flags |= CDEV_NONBLOCK;

	/* Send the request to the driver. */
	if ((r = asynsend3(dp->dmap_driver, &dev_mess, AMF_NOREPLY)) != OK) {
		printf("VFS: asynsend in cdev_io failed: %d\n", r);
		if (GRANT_VALID(gid))
			(void)cpf_revoke(gid);
		return EIO;
	}

	/* Suspend the calling process until a reply arrives. */
	fp->fp_cdev.dev = dev;
	fp->fp_cdev.endpt = dp->dmap_driver;
	fp->fp_cdev.grant = gid;	/* revoke this when unsuspended */
	suspend(FP_BLOCKED_ON_CDEV);

	return SUSPEND;
}

/*
 * Initiate a select call on a device.  Return OK iff the request was sent.
 * This function explicitly bypasses cdev_get() since it must not do CTTY
 * mapping, because a) the caller already has done that, b) "fp" may be wrong.
 */
int
cdev_select(dev_t dev, int ops)
{
	devmajor_t major;
	message dev_mess;
	struct dmap *dp;
	int r;

	/* Determine task dmap, without CTTY mapping. */
	assert(dev != NO_DEV);
	major = major(dev);
	assert(major >= 0 && major < NR_DEVICES);
	assert(major != CTTY_MAJOR);
	dp = &dmap[major];

	/* Prepare the request message. */
	memset(&dev_mess, 0, sizeof(dev_mess));
	dev_mess.m_type = CDEV_SELECT;
	dev_mess.m_vfs_lchardriver_select.minor = minor(dev);
	dev_mess.m_vfs_lchardriver_select.ops = ops;

	/* Send the request to the driver. */
	if ((r = asynsend3(dp->dmap_driver, &dev_mess, AMF_NOREPLY)) != OK) {
		printf("VFS: asynsend in cdev_select failed: %d\n", r);
		return EIO;	/* "not sent", which callers expect */
	}

	return OK;
}

/*
 * Cancel an I/O request, blocking until it has been cancelled.
 */
int
cdev_cancel(dev_t dev, endpoint_t endpt __unused, cp_grant_id_t grant)
{
	devminor_t minor_dev;
	message dev_mess;
	struct dmap *dp;
	int r;

	/* Determine task dmap. */
	if ((dp = cdev_get(dev, &minor_dev)) == NULL)
		return EIO;

	/* Prepare the request message. */
	memset(&dev_mess, 0, sizeof(dev_mess));
	dev_mess.m_type = CDEV_CANCEL;
	dev_mess.m_vfs_lchardriver_cancel.minor = minor_dev;
	dev_mess.m_vfs_lchardriver_cancel.id = fp->fp_endpoint;

	/* Send the request to the driver. */
	if ((r = asynsend3(dp->dmap_driver, &dev_mess, AMF_NOREPLY)) != OK) {
		/*
		 * The cancel cannot be sent. Revoke the grant, so the driver
		 * can no longer touch the buffer, and end the call as a
		 * cancel does; a late reply finds the process not blocked
		 * and is dropped.
		 */
		printf("VFS: asynsend in cdev_cancel failed: %d\n", r);
		if (GRANT_VALID(grant))
			(void)cpf_revoke(grant);
		return EINTR;
	}

	/* Suspend this thread until we have received the response. */
	self->w_task = dp->dmap_driver;
	self->w_drv_sendrec = &dev_mess;

	worker_wait();

	self->w_task = NONE;
	assert(self->w_drv_sendrec == NULL);

	/* Clean up. */
	if (GRANT_VALID(grant))
		(void)cpf_revoke(grant);

	/* Return the result.  Note that the request may have completed. */
	r = dev_mess.m_lchardriver_vfs_reply.status;

	return (r == EAGAIN) ? EINTR : r; /* see below regarding error codes */
}

/*
 * A character driver has results for an open, close, read, write, or ioctl
 * call (i.e., everything except select).  There may be a thread waiting for
 * these results as part of an ongoing open, close, or (for read/write/ioctl)
 * cancel call.  If so, wake up that thread; if not, send a reply to the
 * requesting process. This function MUST NOT block its calling thread.
 */
static void
cdev_generic_reply(message * m_ptr)
{
	struct fproc *rfp;
	struct worker_thread *wp;
	endpoint_t proc_e;
	int r, slot;

	proc_e = m_ptr->m_lchardriver_vfs_reply.id;

	if (m_ptr->m_lchardriver_vfs_reply.status == SUSPEND) {
		printf("VFS: ignoring SUSPEND status from %d\n",
		    m_ptr->m_source);
		return;
	}

	if (isokendpt(proc_e, &slot) != OK) {
		printf("VFS: proc %d from %d not found\n",
		    proc_e, m_ptr->m_source);
		return;
	}
	rfp = &fproc[slot];
	wp = rfp->fp_worker;
	if (wp != NULL && wp->w_task == who_e && wp->w_drv_sendrec != NULL) {
		assert(!fp_is_blocked(rfp));
		*wp->w_drv_sendrec = *m_ptr;
		wp->w_drv_sendrec = NULL;
		worker_signal(wp);	/* continue open/close/cancel */
	} else if (rfp->fp_blocked_on != FP_BLOCKED_ON_CDEV ||
	    rfp->fp_cdev.endpt != m_ptr->m_source) {
		/*
		 * This would typically be caused by a protocol error, i.e., a
		 * driver not properly following the character driver protocol.
		 */
		printf("VFS: proc %d not blocked on %d\n",
		    proc_e, m_ptr->m_source);
	} else {
		/*
		 * Some services use the same infrastructure for nonblocking
		 * and cancelled requests, resulting in one of EINTR or EAGAIN
		 * when the other is really the appropriate code.  Thus,
		 * cdev_cancel converts EAGAIN into EINTR, and we convert EINTR
		 * into EAGAIN here.  TODO: this may be obsolete by now..?
		 */
		r = m_ptr->m_lchardriver_vfs_reply.status;
		revive(proc_e, (r == EINTR) ? EAGAIN : r);
	}
}

/*
 * A character driver has results for us.
 */
void
cdev_reply(void)
{

	if (get_dmap_by_endpt(who_e) == NULL) {
		printf("VFS: ignoring char dev reply from unknown driver %d\n",
		    who_e);
		return;
	}

	switch (call_nr) {
	case CDEV_REPLY:
		cdev_generic_reply(&m_in);
		break;
	case CDEV_SEL1_REPLY:
		select_cdev_reply1(m_in.m_source,
		    m_in.m_lchardriver_vfs_sel1.minor,
		    m_in.m_lchardriver_vfs_sel1.status);
		break;
	case CDEV_SEL2_REPLY:
		select_cdev_reply2(m_in.m_source,
		    m_in.m_lchardriver_vfs_sel2.minor,
		    m_in.m_lchardriver_vfs_sel2.status);
		break;
	default:
		printf("VFS: char driver %u sent unknown reply %x\n",
		    who_e, call_nr);
	}
}

/*
 * The cdevfd(2) back-call: descriptors of a character driver's own device,
 * for a process that is waiting on a request to that driver.  DRM's PRIME
 * and sync files are built on it (the mali service): a buffer or a fence
 * is a minor of the GPU's device, handed to the process as a descriptor in
 * answer to an ioctl, passed on like any descriptor (fork, SCM_RIGHTS over
 * a socket), and named again by its number in an ioctl to the driver,
 * which asks here what it is.
 *
 * CDEVFD_NEW gives the process a new descriptor of the given minor.  No
 * open goes to the driver - it made the thing and counts the open itself -
 * but the last close of the file does, as for any open.  The file is a
 * PFS node, as for a cloned device; lseek(2) to its end says the size the
 * driver gives (a dma-buf's size, which is how Mesa learns it).
 *
 * CDEVFD_GET answers which minor of the caller's device a descriptor of the
 * process is.  Nothing about any other file is said: one of another device
 * is EINVAL, as a closed number is EBADF.
 *
 * What makes it safe to give a driver: the process must be suspended on a
 * request to the caller right now (an ioctl, so its descriptor table is
 * not in use by anyone), and the device is the one that request was made
 * to.  A driver may thus touch only the descriptor tables of processes
 * that are asking it something, and only with files of its own device -
 * less than copyfd(2), which a root service may use on any process and
 * any file.  If the process is being interrupted, unpause() has cleared
 * its blocked state before cancelling, and the call is refused.
 */
int
do_cdevfd(void)
{
  struct fproc *rfp;
  struct filp *f;
  struct vnode *vp;
  struct vmnt *vmp;
  struct node_details res;
  endpoint_t endpt;
  devmajor_t major;
  devminor_t minor;
  mode_t bits;
  uint64_t size;
  int r, slot, fd, flags;

  endpt = job_m_in.m_lsys_vfs_cdevfd.endpt;
  if (isokendpt(endpt, &slot) != OK) return(EINVAL);
  rfp = &fproc[slot];

  if (rfp->fp_blocked_on != FP_BLOCKED_ON_CDEV ||
      rfp->fp_cdev.endpt != who_e)
	return(EPERM);
  major = major(rfp->fp_cdev.dev);
  if (!dmap_driver_match(who_e, major)) return(EPERM);

  switch (job_m_in.m_lsys_vfs_cdevfd.what) {
  case CDEVFD_GET:
	fd = job_m_in.m_lsys_vfs_cdevfd.fd;
	if (fd < 0 || fd >= OPEN_MAX || (f = rfp->fp_filp[fd]) == NULL ||
	    f->filp_count == 0 || f->filp_mode == FILP_CLOSED)
		return(EBADF);
	vp = f->filp_vno;
	if (vp == NULL || !S_ISCHR(vp->v_mode) || major(vp->v_sdev) != major)
		return(EINVAL);
	return(minor(vp->v_sdev));

  case CDEVFD_NEW:
	break;

  default:
	return(EINVAL);
  }

  flags = job_m_in.m_lsys_vfs_cdevfd.flags;
  minor = job_m_in.m_lsys_vfs_cdevfd.minor;
  size = job_m_in.m_lsys_vfs_cdevfd.size;

  if (flags & ~(O_ACCMODE | O_CLOEXEC)) return(EINVAL);
  switch (flags & O_ACCMODE) {
  case O_RDONLY:	bits = R_BIT; break;
  case O_WRONLY:	bits = W_BIT; break;
  case O_RDWR:		bits = R_BIT | W_BIT; break;
  default:		return(EINVAL);
  }
  if (minor < 0 || minor(makedev(major, minor)) != minor) return(EINVAL);
  if ((off_t)size < 0) return(EINVAL);

  /* As memfd_create(2) makes its file: vmnt, vnode, then the descriptor. */
  if ((vmp = find_vmnt(PFS_PROC_NR)) == NULL) return(EIO);
  if ((r = lock_vmnt(vmp, VMNT_READ)) != OK) return(r);

  if ((vp = get_free_vnode()) == NULL) {
	unlock_vmnt(vmp);
	return(err_code);
  }
  lock_vnode(vp, VNODE_OPCL);

  if ((r = get_fd(rfp, 0, bits, &fd, &f)) != OK) {
	unlock_vnode(vp);
	unlock_vmnt(vmp);
	return(r);
  }

  r = req_newnode(PFS_PROC_NR, rfp->fp_effuid, rfp->fp_effgid,
	I_CHAR_SPECIAL | S_IRUSR | S_IWUSR, makedev(major, minor), &res);
  if (r != OK) {
	unlock_filp(f);			/* still unclaimed: count is zero */
	unlock_vnode(vp);
	unlock_vmnt(vmp);
	return(r);
  }

  vp->v_fs_e = res.fs_e;
  vp->v_inode_nr = res.inode_nr;
  vp->v_mode = res.fmode;
  vp->v_uid = res.uid;
  vp->v_gid = res.gid;
  vp->v_size = (off_t)size;
  vp->v_seals = 0;
  vp->v_vmnt = NULL;
  vp->v_dev = NO_DEV;
  vp->v_sdev = makedev(major, minor);
  vp->v_fs_count = 1;
  vp->v_ref_count = 1;

  rfp->fp_filp[fd] = f;
  f->filp_count = 1;
  f->filp_vno = vp;
  f->filp_flags = flags & O_ACCMODE;
  if (flags & O_CLOEXEC)
	FD_SET(fd, &rfp->fp_cloexec_set);

  unlock_filp(f);			/* and the vnode, now the filp has it */
  unlock_vmnt(vmp);

  return(fd);
}
