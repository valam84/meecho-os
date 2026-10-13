/* Memory objects: memfd_create(2).
 *
 * A memory object is pages behind a file descriptor.  A Wayland client
 * draws into it, passes the descriptor to the compositor over a socket
 * (SCM_RIGHTS, which needs nothing from this file - any descriptor goes),
 * and the compositor maps the same pages and reads what was drawn.  So the
 * one thing it must do that no other descriptor here can is to be mapped
 * shared and writable in two processes at once.
 *
 * The work is split three ways, each part where it already lives:
 *
 *   VM keeps the pages (memobj.c there).  It knows the object by the
 *   inode number below, maps its pages into whoever mmap()s it, shared,
 *   and frees them when told to.
 *
 *   PFS keeps an unlinked regular node, the way it keeps one for a pipe:
 *   the size, the times, the owner, for fstat(2) and ftruncate(2).  It
 *   never sees a byte of the contents.
 *
 *   VFS - this file - makes the descriptor, and tells VM when the object
 *   changes size or goes away.  It goes away when the vnode does, and the
 *   vnode is held by every descriptor of every process and by VM itself
 *   for as long as anything maps it (VM holds a descriptor of its own per
 *   mapped file; see fdref.c there).  So "nobody can reach these pages any
 *   more" and "the last reference to the vnode went" are the same event.
 *
 * read(2) and write(2) on the descriptor are not supported: PFS refuses
 * them for a regular node, and nothing that uses these objects needs them
 * - the contents are reached by mapping.  Of the seals, SEAL, SHRINK and
 * GROW are kept here; WRITE and FUTURE_WRITE would need VM to refuse
 * writable mappings, and are refused instead.
 *
 * Entry points:
 *   do_memfd_create:	the memfd_create(2) system call
 *   is_memfd:		is this vnode a memory object?
 *   memfd_release:	the vnode is going: tell VM to free the pages
 *   memfd_truncate:	ftruncate(2) on a memory object
 *   memfd_seals:	F_ADD_SEALS and F_GET_SEALS
 */

#include "fs.h"
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <minix/com.h>
#include <minix/vfsif.h>
#include "file.h"
#include "vnode.h"
#include "vmnt.h"

#define SEALS_KEPT	(F_SEAL_SEAL | F_SEAL_SHRINK | F_SEAL_GROW)

/*===========================================================================*
 *				is_memfd				     *
 *===========================================================================*/
int is_memfd(struct vnode *vp)
{
/* A regular file on PFS: PFS makes nothing else regular. */
  return vp->v_fs_e == PFS_PROC_NR && S_ISREG(vp->v_mode);
}

/*===========================================================================*
 *				memfd_vm				     *
 *===========================================================================*/
static int memfd_vm(int op, struct vnode *vp, off_t size)
{
  message m;

  /* Only a worker thread can wait for VM's answer. */
  if (self == NULL) {
	printf("VFS: memory object %llu: no thread to tell VM\n",
	    (unsigned long long)vp->v_inode_nr);
	return EFAULT;
  }

  memset(&m, 0, sizeof(m));
  m.m_type = VM_MEMFD;
  m.VMMF_OP = op;
  m.VMMF_INO = vp->v_inode_nr;
  m.VMMF_SIZE = (u64_t)size;

  return vm_sendrec(&m);
}

/*===========================================================================*
 *				do_memfd_create				     *
 *===========================================================================*/
int do_memfd_create(void)
{
/* Perform the memfd_create(name, flags) system call. */
  struct vmnt *vmp;
  struct vnode *vp;
  struct filp *filp;
  struct node_details res;
  int r, fd, flags;

  flags = job_m_in.m_lc_vfs_memfd.flags;

  /* MFD_CLOFORK: there is no close-on-fork here at all. */
  if (flags & ~(MFD_CLOEXEC | MFD_ALLOW_SEALING))
	return(EINVAL);

  if ((vmp = find_vmnt(PFS_PROC_NR)) == NULL) {
	printf("VFS: memfd_create(2) without PFS\n");
	return(EIO);
  }
  if ((r = lock_vmnt(vmp, VMNT_READ)) != OK) return(r);

  if ((vp = get_free_vnode()) == NULL) {
	unlock_vmnt(vmp);
	return(err_code);
  }
  lock_vnode(vp, VNODE_OPCL);

  if ((r = get_fd(fp, 0, R_BIT | W_BIT, &fd, &filp)) != OK) {
	unlock_vnode(vp);
	unlock_vmnt(vmp);
	return(r);
  }
  fp->fp_filp[fd] = filp;
  filp->filp_count = 1;

  r = req_newnode(PFS_PROC_NR, fp->fp_effuid, fp->fp_effgid,
	I_REGULAR | S_IRUSR | S_IWUSR, NO_DEV, &res);
  if (r != OK) {
	fp->fp_filp[fd] = NULL;
	filp->filp_count = 0;
	unlock_filp(filp);
	unlock_vnode(vp);
	unlock_vmnt(vmp);
	return(r);
  }

  vp->v_fs_e = res.fs_e;
  vp->v_mapfs_e = res.fs_e;
  vp->v_inode_nr = res.inode_nr;
  vp->v_mapinode_nr = res.inode_nr;
  vp->v_mode = res.fmode;
  vp->v_uid = res.uid;
  vp->v_gid = res.gid;
  vp->v_fs_count = 1;
  vp->v_mapfs_count = 1;
  vp->v_ref_count = 1;
  vp->v_size = 0;
  vp->v_vmnt = NULL;
  vp->v_dev = NO_DEV;
  vp->v_sdev = NO_DEV;
  /* Without MFD_ALLOW_SEALING the object is sealed against sealing. */
  vp->v_seals = (flags & MFD_ALLOW_SEALING) ? 0 : F_SEAL_SEAL;

  filp->filp_vno = vp;
  filp->filp_flags = O_RDWR;
  if (flags & MFD_CLOEXEC)
	FD_SET(fd, &fp->fp_cloexec_set);

  unlock_filp(filp);		/* and the vnode, now that the filp has it */
  unlock_vmnt(vmp);

  return(fd);
}

/*===========================================================================*
 *				memfd_release				     *
 *===========================================================================*/
void memfd_release(struct vnode *vp)
{
/* The last reference to a memory object's vnode is going.  VM is told
 * before PFS is: once PFS has freed the inode, the next memfd_create(2)
 * may be given the same number, and VM must not still hold pages under it.
 */
  int r;

  if ((r = memfd_vm(VMMF_FREE, vp, 0)) != OK)
	printf("VFS: VM did not free memory object %llu: %d\n",
	    (unsigned long long)vp->v_inode_nr, r);
}

/*===========================================================================*
 *				memfd_truncate				     *
 *===========================================================================*/
int memfd_truncate(struct vnode *vp, off_t newsize)
{
  int r;

  if (newsize < vp->v_size && (vp->v_seals & F_SEAL_SHRINK))
	return(EPERM);
  if (newsize > vp->v_size && (vp->v_seals & F_SEAL_GROW))
	return(EPERM);

  if ((r = req_ftrunc(vp->v_fs_e, vp->v_inode_nr, newsize, 0)) != OK)
	return(r);

  /* Shrinking: what lies past the end is zero from now on (VM). */
  if ((r = memfd_vm(VMMF_TRUNC, vp, newsize)) != OK)
	printf("VFS: VM did not truncate memory object %llu: %d\n",
	    (unsigned long long)vp->v_inode_nr, r);

  vp->v_size = newsize;
  return(OK);
}

/*===========================================================================*
 *				memfd_seals				     *
 *===========================================================================*/
int memfd_seals(struct filp *f, int req, int seals)
{
  struct vnode *vp = f->filp_vno;

  if (!is_memfd(vp))
	return(EINVAL);

  if (req == F_GET_SEALS)
	return(vp->v_seals);

  if (!(f->filp_mode & W_BIT))
	return(EPERM);
  if (vp->v_seals & F_SEAL_SEAL)
	return(EPERM);
  if (seals & ~SEALS_KEPT)
	return(EINVAL);

  vp->v_seals |= seals;
  return(OK);
}
