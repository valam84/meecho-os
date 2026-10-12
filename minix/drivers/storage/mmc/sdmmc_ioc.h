/*
 * The one request of this driver that is its own: hand over the tail of its
 * journal.
 *
 * Why it exists: on the target board the console is the only place a
 * driver's messages go, and at 1.5 Mbaud through the USB bridge it loses
 * characters in the middle of lines - a register dump with a hole in it, and
 * on a busy machine most of every debug line. The journal is therefore kept
 * in memory as well (sdmmc_ring.c), and this request copies the most recent
 * part of it out, so that it can be read over ssh by port/test/sdmmc/sdlog.c
 * instead of reconstructed from what the console happened to let through.
 *
 * The size is bounded by what an ioctl number can describe (twelve bits).
 */

#ifndef _SDMMC_IOC_H
#define _SDMMC_IOC_H

#include <sys/ioccom.h>
#include <stdint.h>

struct sdmmc_logdump {
	uint64_t	total;		/* bytes ever written to the journal */
	uint32_t	len;		/* bytes of text below, oldest first */
	char		text[4072];
};

/*
 * Below 4096, and not by accident. MINIX keeps twelve bits of length in a
 * request number (IOCPARM_MASK 0xfff, where NetBSD has thirteen); the first
 * version was 8192 bytes, the field kept none of it, VFS made a grant of no
 * bytes, and the copy out was refused with EPERM - which reads like a
 * permission problem and is a size.
 */
typedef char sdmmc_logdump_fits[(sizeof(struct sdmmc_logdump) <= 4095) ? 1 : -1];

#define SDMMC_IOC_LOG	_IOR('D', 0x5d, struct sdmmc_logdump)

#endif /* _SDMMC_IOC_H */
