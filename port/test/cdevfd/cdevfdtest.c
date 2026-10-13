/*
 * A character driver that does nothing but cdevfd(2): the VFS back-call
 * that gives a process a descriptor of one of the driver's minors, or
 * names the minor of a descriptor the process holds.  It is what DRM's
 * PRIME and sync files of the mali service are built on, tested here on
 * QEMU, without a GPU (qemu-cdevfd.py, client cdevfdt.c).
 *
 * Minor 0 is the control node.  Minors from CFT_OBJ are "objects": they are
 * never opened, only made by CFT_NEW, and the driver counts their opens
 * the way the mali service counts a dma-buf's - one up for each
 * descriptor VFS made, one down for each close VFS sends.  CFT_OPENS
 * answers the count, which is what the client checks after dup, fork,
 * exec, a pass over a socket, and close.
 *
 *	minix-service up cdevfdtest -dev /dev/cdevfdtest -config cdevfdtest.conf
 */

#include <minix/drivers.h>
#include <minix/chardriver.h>
#include <minix/syslib.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>

#include "cdevfd.h"

static int opens[CFT_NOBJ];
static int errors;

static int
cft_open(devminor_t minor, int access, endpoint_t user)
{
	return minor == 0 ? OK : ENXIO;
}

static int
cft_close(devminor_t minor)
{
	if (minor >= CFT_OBJ && minor < CFT_OBJ + CFT_NOBJ &&
	    --opens[minor - CFT_OBJ] < 0) {
		printf("cdevfdtest: minor %d closed more than it was made\n",
		    minor);
		errors++;
	}
	return OK;
}

static int
counted(int minor)
{
	return minor >= CFT_OBJ && minor < CFT_OBJ + CFT_NOBJ;
}

static int
cft_ioctl(devminor_t minor, unsigned long request, endpoint_t endpt,
	cp_grant_id_t grant, int flags, endpoint_t user, cdev_id_t id)
{
	struct cft a;
	int r;

	if (minor != 0 && !counted(minor))
		return ENXIO;
	if (IOCGROUP(request) != 'Z' || IOCPARM_LEN(request) != sizeof(a))
		return ENOTTY;
	if ((r = sys_safecopyfrom(endpt, grant, 0, (vir_bytes)&a,
	    sizeof(a))) != OK)
		return r;

	switch (request) {
	case CFT_NEW:
		/* Counted first: a close of another descriptor of it may come
		 * while VFS makes this one. */
		if (counted(a.minor))
			opens[a.minor - CFT_OBJ]++;
		/* Requests for it are to be served: libchardriver drops those
		 * for a minor it saw no open of. */
		if ((a.result = chardriver_open_minor(a.minor)) == OK)
			a.result = cdevfd_new(user, a.minor, a.flags, a.size);
		if (a.result < 0 && counted(a.minor))
			opens[a.minor - CFT_OBJ]--;
		a.fd = a.result;
		break;
	case CFT_GET:
		a.result = cdevfd_get(user, a.fd);
		a.minor = a.result;
		break;
	case CFT_OPENS:
		a.result = counted(a.minor) ? opens[a.minor - CFT_OBJ] : -1;
		break;
	case CFT_WHO:
		a.minor = a.result = minor;
		break;
	case CFT_PROBE:
		/* What VFS refuses. */
		a.r[0] = cdevfd_get(sef_self(), 0);	/* not waiting on us */
		a.r[1] = cdevfd_new(INIT_PROC_NR, CFT_OBJ, O_RDWR, 0);
		a.r[2] = cdevfd_new(user, CFT_OBJ, O_RDWR | O_APPEND, 0);
		a.r[3] = cdevfd_new(user, -1, O_RDWR, 0);
		a.r[4] = cdevfd_new(user, 0x200000, O_RDWR, 0);
		a.r[5] = cdevfd_get(user, -1);
		a.r[6] = cdevfd_get(user, OPEN_MAX);
		a.r[7] = cdevfd_new(user, CFT_OBJ, O_RDWR, ~0ULL);
		a.result = errors;
		break;
	default:
		return ENOTTY;
	}
	return sys_safecopyto(endpt, grant, 0, (vir_bytes)&a, sizeof(a));
}

static struct chardriver cft_tab = {
	.cdr_open	= cft_open,
	.cdr_close	= cft_close,
	.cdr_ioctl	= cft_ioctl,
};

static int
cft_init(int type, sef_init_info_t *info)
{
	chardriver_announce();
	return OK;
}

int
main(void)
{
	sef_setcb_init_fresh(cft_init);
	sef_setcb_init_restart(cft_init);
	sef_startup();
	chardriver_task(&cft_tab);
	return OK;
}
