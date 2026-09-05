/*
 * No PC keyboard on AArch64. Input arrives on the serial line, which rs232.c
 * handles; these are the entry points tty calls for a keyboard controller,
 * and they do nothing, as ARM's do.
 */
#include <minix/ipc.h>
#include <sys/termios.h>
#include "tty.h"

void
do_fkey_ctl(message *m)
{
}

void
do_input(message *m)
{
}

void
kb_init_once(void)
{
}

int
kbd_loadmap(endpoint_t endpt, cp_grant_id_t grant)
{
	return 0;
}

void
kb_init(tty_t *tp)
{
}
