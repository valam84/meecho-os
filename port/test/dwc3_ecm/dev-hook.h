/*
 * dwc3_ecm stand: the service's register access to the DWC3 core goes to
 * the model in dwtest.c.
 *
 * run.sh puts this file in front of dwc3_dev.c and dwc3_ep0.c (-include):
 * it includes the real dwc3_dev.h (where dwc3_rd()/dwc3_wr() are plain
 * reads and writes of the core's window) and then renames their calls to
 * the model's.  The service is not changed by a line; the model sees every
 * register write the moment the service makes it, as the core does - an
 * endpoint command is carried out at once, a soft reset ends at once, the
 * event count is the model's.
 */
#ifndef _DEV_HOOK_H
#define _DEV_HOOK_H
#include "dwc3_dev.h"
uint32_t model_rd(unsigned off);
void model_wr(unsigned off, uint32_t v);
#define dwc3_rd(off)		model_rd(off)
#define dwc3_wr(off, v)		model_wr((off), (v))
#endif
