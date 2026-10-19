/*
 * MEECHO: diag-router's USB host (dist/router/usb.c) is Linux's
 * FunctionFS gadget, and its UART host (dist/router/uart.c) sets the line
 * with Linux's TCGETS; neither is here.  The router still takes the
 * peripherals' streams over QRTR and serves its unix socket and its TCP
 * client (-s).
 */

#include "diag.h"

int
diag_usb_open(const char *ffs_name)
{
	(void)ffs_name;
	return -1;
}

int
diag_uart_open(const char *uartname, unsigned int baudrate)
{
	(void)uartname;
	(void)baudrate;
	return -1;
}
