/*
 * xHCI ring stand: the driver's register writes go to the model as they
 * are made (model_mmio_wr()), because some of them - acknowledging a
 * change bit of PORTSC - store the very value that was read, and a model
 * that only compared memory with what it last wrote would never see them.
 *
 * run.sh puts this file in front of the driver's files (-include): it
 * includes the driver's own xhci.h, where xhci_wr() is a plain store, and
 * renames its calls.  The driver is not changed by a line.
 */
#ifndef _MMIOHOOK_H
#define _MMIOHOOK_H
#include "xhci.h"
void model_mmio_wr(vir_bytes block, unsigned off, uint32_t v);
#define xhci_wr(b, o, v)	model_mmio_wr((b), (o), (v))
#endif
