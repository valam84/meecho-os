/*
 * An AArch64 machine normally uses the serial line from rs232.c as its
 * console.  Some machines instead hand the kernel a simple framebuffer and
 * have no usable serial port: the Mi Pad 4 is such a machine.  For those,
 * console=console selects ttyc0 and this file makes it a real tty.  Its
 * output is sent through SYS_DIAGCTL, so the kernel's already-running
 * framebuffer console draws it.  Kernel messages that tty feeds back to the
 * line are consumed without being sent to SYS_DIAGCTL again; the kernel has
 * already drawn those, and feeding them back would make an echo loop.
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
#define SCREEN_OUT_BYTES 256

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

static int
screen_read(tty_t *tp, int try)
{
	/* Keyboard events are placed directly in tty's input queue. */
	return try && tp->tty_incount > 0;
}

static void
screen_done(tty_t *tp, int count)
{
	endpoint_t caller = tp->tty_outcaller;

	tp->tty_outcum += count;
	if ((tp->tty_outleft -= count) != 0) {
		tp->tty_events = 1;
		return;
	}

	if (caller != KERNEL)
		chardriver_reply_task(caller, tp->tty_outid, tp->tty_outcum);
	tp->tty_outcum = 0;
	tp->tty_outcaller = NONE;
}

static int
screen_write(tty_t *tp, int try)
{
	char buf[SCREEN_OUT_BYTES];
	int count, ocount, r;

	if (tp->tty_outleft == 0 || tp->tty_inhibited)
		return 0;
	if (try)
		return 1;

	/* The diagnostic has already appeared on the kernel console. */
	if (tp->tty_outcaller == KERNEL) {
		count = tp->tty_outleft;
		screen_done(tp, count);
		return 1;
	}

	/* Leave room for output processing to expand every LF to CR+LF. */
	count = tp->tty_outleft;
	if (count > (int)sizeof(buf) / 2)
		count = sizeof(buf) / 2;
	if ((r = sys_safecopyfrom(tp->tty_outcaller, tp->tty_outgrant,
	    tp->tty_outcum, (vir_bytes)buf, count)) != OK)
		return 0;

	ocount = sizeof(buf);
	out_process(tp, buf, buf, buf + sizeof(buf), &count, &ocount);
	if (count == 0)
		return 0;

	tp->tty_reprint = TRUE;
	cons_mirror(tp, buf, buf, buf + sizeof(buf), ocount);
	if (ocount > 0)
		(void)sys_diagctl_diag(buf, ocount);
	screen_done(tp, count);

	return 1;
}

static void
screen_echo(tty_t *tp, int character)
{
	char buf[2];
	int count = 1, ocount = sizeof(buf);

	buf[0] = character;
	out_process(tp, buf, buf, buf + sizeof(buf), &count, &ocount);
	if (ocount > 0) {
		cons_mirror(tp, buf, buf, buf + sizeof(buf), ocount);
		(void)sys_diagctl_diag(buf, ocount);
	}
}

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
	/* There is one framebuffer console; the other ttyc lines stay absent. */
	if (tp != &tty_table[0])
		return;

	tp->tty_devread = screen_read;
	tp->tty_devwrite = screen_write;
	tp->tty_echo = screen_echo;

	if (consoleline == CONS_MINOR)
		printf("TTY: framebuffer console active\n");
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
