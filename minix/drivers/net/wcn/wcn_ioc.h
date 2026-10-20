#ifndef _WCN_IOC_H
#define _WCN_IOC_H

/*
 * /dev/wlan0, the wcn service's character device (major 136): what the
 * daemon that speaks QMI with the WLAN firmware (wlfw, in the modem) asks
 * of the service, which holds the rights the daemon has not
 * (port/clover/hw-wifi.md, section 9).
 *
 *   WCNIOC_MSA		where the MSA is (the "wlan-msa-mem" of the tree)
 *			and what of it is given away - what MSA_INFO names
 *   WCNIOC_ASSIGN	give a region the firmware named in its answer to
 *			MSA_INFO to the modem and the WLAN (TrustZone's
 *			MP/ASSIGN: HLOS -> MSS_MSA, WLAN, and WLAN_CE unless
 *			the region is secure; read-write) - Linux's
 *			ath10k_qmi_map_msa_permission()
 *   WCNIOC_RECLAIM	take every given region back (to HLOS) - only once
 *			the firmware is gone: memory the modem uses taken
 *			away from it is a fault on its side
 *   WCNIOC_POWER	the WLAN's supplies and reference clock on (1) or
 *			off (0) - before WLAN_CFG/WLAN_MODE, as Linux
 *   WCNIOC_START	after WLAN_MODE: the copy engines up, HTC READY,
 *			WMI control and HTT data connected
 *
 * Root only (the node is 0600).
 */

#include <sys/ioccom.h>
#include <stdint.h>

#define WCN_REGIONS	2

struct wcn_msa {
	uint64_t pa;
	uint32_t size;
	uint32_t given;			/* regions given away now */
};

struct wcn_region {
	uint64_t addr;
	uint32_t size;
	uint32_t secure;
};

#define WCNIOC_MSA	_IOR('W', 1, struct wcn_msa)
#define WCNIOC_ASSIGN	_IOW('W', 2, struct wcn_region)
#define WCNIOC_RECLAIM	_IO('W', 3)
#define WCNIOC_POWER	_IOW('W', 4, int)
#define WCNIOC_START	_IO('W', 5)

#endif /* _WCN_IOC_H */
