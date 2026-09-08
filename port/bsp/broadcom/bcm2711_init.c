#include <sys/types.h>
#include "bsp_init.h"
#include "bsp_padconf.h"
#include "bsp_reset.h"

/*
 * Board level initialisation for the BCM2711.
 *
 * The TI equivalent (bsp/ti/omap_init.c) also brings up an RTC here. The
 * Compute Module 4 has no battery-backed real time clock at all, so there is
 * nothing to initialise and the system starts at the epoch until something
 * sets the time. Anything that needs wall-clock time across reboots has to
 * get it from the network or from an RTC on the carrier board.
 */
void
bsp_init(void)
{

	/* map memory for padconf */
	bsp_padconf_init();

	/* map memory for reset control */
	bsp_reset_init();

	/* disable watchdog */
	bsp_disable_watchdog();
}
