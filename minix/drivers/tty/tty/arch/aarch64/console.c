/*
 * No video console on AArch64: the console is a serial line, driven by
 * rs232.c. These are the entry points tty calls for a frame buffer, and they
 * do nothing, the same way ARM's do.
 *
 * A real one would need a display, and neither target has one the kernel
 * talks to: QEMU's virt machine has no framebuffer at all unless one is added
 * on the command line, and on a compute module the display is behind a driver
 * far larger than this file.
 */
#include <minix/ipc.h>
#include <sys/termios.h>
#include "tty.h"

void
do_video(message *m, int ipc_status)
{
}

void
scr_init(tty_t *tp)
{
}

void
cons_stop(void)
{
}

void
beep_x(unsigned int freq, clock_t dur)
{
}

int
con_loadfont(endpoint_t endpt, cp_grant_id_t grant)
{
	return 0;
}
