/*
 * The console on AArch64 is a serial line, driven by rs232.c.  This file
 * holds what tty calls for a video console - which is still nothing: there
 * is no text-mode screen to drive - and one thing that is new: a copy of
 * the console line's output for a screen driver to show.
 *
 * A screen here is a frame buffer behind a separate driver (fb, with VOP2
 * and HDMI on the CB2), and the text has to be drawn by whoever owns the
 * pixels.  So tty does not draw; it keeps every byte that goes out on the
 * console line - kernel messages, rc, getty, echo, after output processing,
 * exactly what the serial terminal receives - in a ring of its own, and
 * hands the screen driver a read-only grant to it (TTY_SCREEN_UP, in
 * <minix/com.h>).  After that each write is followed by a notify(), which
 * never blocks and collapses with any notify not yet taken; the screen
 * pulls what is new.  A screen that is slow, wedged or gone costs tty
 * nothing: it falls behind, the ring overwrites what it did not take, and
 * the console line itself is not touched.
 *
 * The ring starts filling at tty's start, not at the screen's, so the
 * screen comes up with the boot so far on it, as far back as the ring
 * reaches.
 */
#include <minix/drivers.h>
#include <minix/ipc.h>
#include <minix/safecopies.h>
#include <sys/termios.h>
#include <stdint.h>
#include <string.h>
#include "tty.h"

#define MIRROR_BYTES	16384

/*
 * What the grant covers: the count of bytes ever written, then the text,
 * byte n of the stream at data[n % MIRROR_BYTES].  The count is written
 * after the bytes it counts, so a reader that sees it has the bytes - and a
 * reader that took longer than the ring's length to copy them sees the
 * count run past what it started from, and knows to drop what it read.
 */
static struct {
	uint64_t	written;
	char		data[MIRROR_BYTES];
} mirror;

static endpoint_t screen = NONE;
static cp_grant_id_t screen_grant = GRANT_INVALID;

void
cons_mirror(tty_t *tp, const char *bstart, const char *bpos,
	const char *bend, int count)
{
	uint64_t w;

	if (tp != line2tty(consoleline) || count <= 0)
		return;

	w = mirror.written;
	while (count-- > 0) {
		mirror.data[w++ % MIRROR_BYTES] = *bpos;
		if (++bpos >= bend)
			bpos = bstart;
	}
	/*
	 * Release order: the screen may copy on another core while this one
	 * writes, and must not see the count before the bytes.
	 */
	__atomic_store_n(&mirror.written, w, __ATOMIC_RELEASE);

	if (screen != NONE)
		(void)ipc_notify(screen);
}

/*
 * A screen driver has come up.  It asked with sendrec, so the reply cannot
 * block; a restarted driver is a new endpoint, and the grant made for the
 * old one goes first.
 */
void
do_screen_up(message *m)
{
	message reply;
	endpoint_t ep = m->m_source;

	if (screen_grant != GRANT_INVALID)
		(void)cpf_revoke(screen_grant);
	screen = NONE;

	memset(&reply, 0, sizeof(reply));
	screen_grant = cpf_grant_direct(ep, (vir_bytes)&mirror, sizeof(mirror),
	    CPF_READ);
	if (screen_grant == GRANT_INVALID) {
		reply.m_type = ENOMEM;
	} else {
		reply.m_type = OK;
		reply.m_u32.data[0] = (uint32_t)screen_grant;
		reply.m_u32.data[1] = MIRROR_BYTES;
	}
	if (ipc_send(ep, &reply) == OK && reply.m_type == OK)
		screen = ep;
}

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
