/*
 * dwc3_ecm stand: what the driver takes from <minix/netdriver.h>, with the
 * frame buffer that libnetdriver keeps opaque made concrete, and the
 * constants the driver uses.
 */
#ifndef _STUB_MINIX_NETDRIVER_H
#define _STUB_MINIX_NETDRIVER_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#define NDEV_HWADDR_MAX		6
#define NDEV_ETH_PACKET_MIN	60	/* <minix/const.h> */
#define NDEV_ETH_PACKET_MAX	1514
#define NDEV_ETH_PACKET_MAX_TAGGED (NDEV_ETH_PACKET_MAX + 4)
#define NDEV_CAP_MCAST		0x20000000
#define NDEV_CAP_BCAST		0x40000000
#define NDEV_LINK_UP		1
#define NDEV_LINK_DOWN		2
#define IFM_ETHER		0x00000020
#define IFM_FDX			0x00100000
#define IFM_1000_T		16
#ifndef SUSPEND
#define SUSPEND			(-998)
#endif

struct netdriver_data {
	uint8_t *buf;
	size_t len;
};

typedef struct {
	uint8_t na_addr[NDEV_HWADDR_MAX];
} netdriver_addr_t;

typedef struct { int m_type, m_source; } message;

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

void netdriver_task(const struct netdriver * ndp);
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
