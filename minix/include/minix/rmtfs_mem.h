/*
 * /dev/qcom_rmtfs_mem<N>: the memory a Qualcomm modem shares with rmtfs,
 * the daemon that serves it its file systems (the qcom_rmtfs_mem service,
 * minix/drivers/remoteproc/qcom_rmtfs_mem; Linux's rmtfs_mem.c).  read(2)
 * and write(2) at an offset into it - pread/pwrite, what rmtfs does when it
 * has no mapping - and one request for where it is: rmtfs tells the modem
 * physical addresses.  N is the tree's qcom,client-id (1 for the modem).
 */
#ifndef _MINIX_RMTFS_MEM_H
#define _MINIX_RMTFS_MEM_H

#include <sys/ioccom.h>
#include <stdint.h>

struct rmtfs_mem_info {
	uint64_t phys;		/* its first byte, as the modem names it */
	uint64_t size;
	uint32_t client_id;
	uint32_t pad;
};

#define RMTFS_MEM_INFO		_IOR('R', 1, struct rmtfs_mem_info)

#endif /* _MINIX_RMTFS_MEM_H */
