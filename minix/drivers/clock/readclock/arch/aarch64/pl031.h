#ifndef __PL031_H
#define __PL031_H

#include <minix/fdt.h>

#include "readclock.h"

/*
 * ARM PrimeCell PL031: a 32-bit counter of seconds, and nothing else worth
 * the name of a calendar. It counts from whatever was last loaded into it;
 * on QEMU's virt it starts at the host's time, on hardware at whatever the
 * battery kept. The system reads it as seconds since the epoch, which is
 * what everyone who ever wrote to one meant by it.
 */
#define PL031_RTCDR	0x000	/* the count; read-only */
#define PL031_RTCMR	0x004	/* match, for the alarm interrupt */
#define PL031_RTCLR	0x008	/* load: a write sets the count */
#define PL031_RTCCR	0x00c	/* control: bit 0 starts the clock */
#define PL031_RTCIMSC	0x010	/* interrupt mask */
#define PL031_RTCRIS	0x014	/* raw interrupt status */
#define PL031_RTCMIS	0x018	/* masked interrupt status */
#define PL031_RTCICR	0x01c	/* interrupt clear */
#define PL031_PERIPHID0	0xfe0	/* PrimeCell identification: part number */
#define PL031_PERIPHID1	0xfe4	/* ...low nibble is its high byte */

#define PL031_RTCCR_START	0x1
#define PL031_PART_NUMBER	0x031

#define PL031_MIN_SIZE		0x1000

int pl031_probe(const struct fdt_node *node, struct rtc *r);

#endif /* __PL031_H */
