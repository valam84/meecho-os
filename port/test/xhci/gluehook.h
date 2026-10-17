/*
 * xHCI glue stand: every register access of the glue and core files goes
 * to gluetest.c, which records the writes in order.
 *
 * gluetrace.sh puts this file in front of xhci_find.c, xhci_rk.c,
 * xhci_dwc3.c and xhci_qcom.c (-include): it includes the driver's own
 * xhci.h, where xhci_rd()/xhci_wr() are plain loads and stores, and then
 * renames their calls.  The driver is not changed by a line.
 */
#ifndef _GLUEHOOK_H
#define _GLUEHOOK_H
#include "xhci.h"
uint32_t trace_rd(vir_bytes block, unsigned off);
void trace_wr(vir_bytes block, unsigned off, uint32_t v);
#define xhci_rd(b, o)		trace_rd((b), (o))
#define xhci_wr(b, o, v)	trace_wr((b), (o), (v))
#endif
