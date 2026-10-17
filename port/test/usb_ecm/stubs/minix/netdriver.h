/*
 * usb_ecm stand: what the driver takes from <minix/netdriver.h> and the
 * headers around it, with libnetdriver's opaque frame made concrete.  The
 * constants are the system's (<minix/const.h>, <minix/com.h>,
 * <net/if_media.h>); the stand plays libnetdriver and lwip (ecmtest.c).
 */
#ifndef _STUB_MINIX_NETDRIVER_H
#define _STUB_MINIX_NETDRIVER_H

#include <minix/drivers.h>

#define NDEV_HWADDR_MAX		6
#define NDEV_ETH_PACKET_MIN	60
#define NDEV_ETH_PACKET_MAX	1514
#define NDEV_ETH_PACKET_MAX_TAGGED (NDEV_ETH_PACKET_MAX + 4)

#define NDEV_LINK_UNKNOWN	0
#define NDEV_LINK_UP		1
#define NDEV_LINK_DOWN		2

#define NDEV_MODE_DOWN		0x00
#define NDEV_MODE_UP		0x01
#define NDEV_MODE_BCAST		0x02
#define NDEV_MODE_MCAST_LIST	0x04
#define NDEV_MODE_MCAST_ALL	0x08
#define NDEV_MODE_PROMISC	0x10

#define IFM_ETHER		0x00000020
#define IFM_AUTO		0
#define IFM_10_T		3
#define IFM_100_TX		6
#define IFM_1000_T		16
#define IFM_FDX			0x00100000

struct netdriver_data {
	uint8_t buf[2048];
	size_t len;
};

typedef struct {
	uint8_t na_addr[NDEV_HWADDR_MAX];
} netdriver_addr_t;

struct netdriver {
	const char *ndr_name;
	int (* ndr_init)(unsigned int instance, netdriver_addr_t * hwaddr,
	    uint32_t * caps, unsigned int * ticks);
	void (* ndr_stop)(void);
	void (* ndr_set_mode)(unsigned int mode,
	    const netdriver_addr_t * mcast_list, unsigned int mcast_count);
	void (* ndr_set_caps)(uint32_t caps);
	void (* ndr_set_flags)(uint32_t flags);
	void (* ndr_set_media)(uint32_t media);
	void (* ndr_set_hwaddr)(const netdriver_addr_t * hwaddr);
	ssize_t (* ndr_recv)(struct netdriver_data * data, size_t max);
	int (* ndr_send)(struct netdriver_data * data, size_t size);
	unsigned int (* ndr_get_link)(uint32_t * media);
	void (* ndr_intr)(unsigned int mask);
	void (* ndr_tick)(void);
	void (* ndr_other)(const message * m_ptr, int ipc_status);
};

int netdriver_init(const struct netdriver * ndp);
void netdriver_process(const struct netdriver * ndp, const message * m_ptr,
	int ipc_status);
void netdriver_terminate(void);
const char *netdriver_name(void);
void netdriver_recv(void);
void netdriver_send(void);
void netdriver_link(void);
void netdriver_stat_oerror(uint32_t count);
void netdriver_stat_ierror(uint32_t count);
void netdriver_copyin(struct netdriver_data * data, size_t off,
	void * ptr, size_t size);
void netdriver_copyout(struct netdriver_data * data, size_t off,
	const void * ptr, size_t size);

#endif
