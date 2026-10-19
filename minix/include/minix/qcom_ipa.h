/*
 * /dev/ipa: the IPA service (minix/drivers/net/qcom_ipa) to the daemon that
 * speaks QMI with the modem's IPA driver for it (ipaqmid) - the service
 * owns the hardware, the daemon the socket (a service waiting on a socket
 * through VFS would be a service VFS waits on, waiting on VFS; the Wi-Fi's
 * wcn and wcnd split the same way).  port/clover/hw-modem.md, section 9.
 */
#ifndef _MINIX_QCOM_IPA_H
#define _MINIX_QCOM_IPA_H

#include <sys/ioccom.h>
#include <stdint.h>

/* What the service has done; IPAIOC_INFO's state. */
#define IPA_ST_SRAM	0x01	/* reset, pipes up, SRAM filled */
#define IPA_ST_UC	0x02	/* the microcontroller said it is up */
#define IPA_ST_MODEM	0x04	/* the handshake is complete (IPAIOC_MODEM) */

/*
 * The SRAM's parts the modem is told about in INIT_DRIVER, as offsets
 * from the start of the IPA's shared memory: for the tables, "end" is the
 * last index (routing) as the message wants it, for the others the last
 * byte.
 */
struct ipa_info {
	uint32_t state;
	uint32_t hdr_start, hdr_end;		/* the modem's headers */
	uint32_t rt4_start, rt4_end;		/* routing, v4 and v6 */
	uint32_t rt6_start, rt6_end;
	uint32_t ft4_start, ft6_start;		/* filtering */
	uint32_t mdm_start, mdm_size;		/* the modem's own part */
	uint32_t comp_start, comp_end;		/* compression commands */
};

#define IPAIOC_INFO	_IOR('I', 1, struct ipa_info)
#define IPAIOC_MODEM	_IOW('I', 2, int)	/* 1: handshake done, 0: gone */

#endif /* _MINIX_QCOM_IPA_H */
