#ifndef _DWMAC_H
#define _DWMAC_H

#include <minix/drivers.h>
#include <minix/log.h>
#include <minix/netdriver.h>

/*
 * A Synopsys DWMAC 4/5 with the Rockchip glue around it - the network
 * controller of the RK3566/RK3568, and so of the BIGTREETECH CB2.
 *
 * The driver is in four parts, and the split is by what a part talks to:
 *
 *   dwmac_find.c  the device tree: what this machine has and where
 *   dwmac_rk.c    the glue: clocks, reset, pins, delay lines, link speed
 *   dwmac_mdio.c  the PHY, over the controller's own MDIO bus
 *   dwmac.c       libnetdriver, and the driver's own life
 *
 * The glue is the part that is not in any Synopsys document, and the part
 * that was read out of a working system before it was written; see
 * port/cb2-gmac/registers.md.
 */

#define DWMAC_MAX_RESETS	4

/*
 * The rings.  Sixty-four descriptors each way is eight cache lines' worth
 * of them at the assumed line size, which is what the receive side refills
 * in one go; the buffers are one per descriptor and big enough for any
 * ethernet frame, so a packet never spans two.
 */
#define DWMAC_RX_DESCS		64
#define DWMAC_TX_DESCS		64
#define DWMAC_BUF_SIZE		2048

/* Everything the device tree said about this controller. */
struct dwmac_devinfo {
	phys_bytes base;		/* the controller */
	size_t size;
	int irq;

	phys_bytes grf_base;		/* the general register file */
	size_t grf_size;

	phys_bytes cru_base;		/* clocks and resets */
	size_t cru_size;

	phys_bytes gpio_base;		/* the bank that resets the PHY */
	size_t gpio_size;
	int reset_pin;			/* -1 when the tree names none */
	int reset_active_low;
	unsigned reset_delay_us[3];	/* before, held, after */

	unsigned reset_id[DWMAC_MAX_RESETS];
	unsigned nresets;

	unsigned tx_delay;		/* RGMII delay lines, 0..0x7f */
	unsigned rx_delay;
	int clock_from_phy;		/* clock_in_out = "input" */
	int phy_addr;			/* on the MDIO bus; -1 if unsaid */
};

/* And what the driver made of it. */
struct dwmac {
	struct dwmac_devinfo info;

	vir_bytes mac;			/* mapped register blocks */
	vir_bytes grf;
	vir_bytes cru;
	vir_bytes gpio;

	int irq_hook;
	int irq_enabled;

	/* The rings, in this address space and in the device's. */
	vir_bytes rx_ring;
	phys_bytes rx_ring_phys;
	vir_bytes tx_ring;
	phys_bytes tx_ring_phys;
	vir_bytes rx_buf;
	phys_bytes rx_buf_phys;
	vir_bytes tx_buf;
	phys_bytes tx_buf_phys;

	int rx_next;			/* the descriptor to look at next */
	int tx_head;			/* where the driver writes */
	int tx_tail;			/* how far the device has got */
	unsigned rx_errors;
	unsigned irqs;			/* how many interrupts arrived */
	int phy_loopback;		/* diagnostic: loop at the PHY */
	int tx_sweep;			/* diagnostic: walk the transmit delay */
	unsigned tx_sweep_idx;
	int clk_sweep;			/* diagnostic: walk the transmit clock */
	unsigned clk_sweep_idx;

	unsigned speed;			/* 0 when the link is down */
	int full_duplex;
	uint32_t phy_id;

	netdriver_addr_t hwaddr;
};

extern struct dwmac dwmac;
extern struct log dwmac_log;

/* dwmac_find.c */
int dwmac_find(struct dwmac_devinfo *info, int skip);

/* dwmac_rk.c */
int dwmac_rk_map(void);
void dwmac_rk_clocks_on(void);
void dwmac_rk_reset_controller(void);
void dwmac_rk_pins(void);
void dwmac_rk_rgmii(void);
void dwmac_rk_set_speed(unsigned speed);
void dwmac_rk_set_txdelay(unsigned tx_delay);
void dwmac_rk_set_speed_sel(unsigned sel);
void dwmac_rk_reset_phy(void);

/* dwmac_ring.c */
int dwmac_ring_alloc(void);
void dwmac_ring_init(void);
void dwmac_ring_free(void);
ssize_t dwmac_ring_recv(struct netdriver_data *data, size_t max);
int dwmac_ring_send(struct netdriver_data *data, size_t size);

/* dwmac_mdio.c */
int dwmac_mdio_read(int phyaddr, int reg, uint16_t *val);
int dwmac_mdio_write(int phyaddr, int reg, uint16_t val);
int dwmac_phy_find(void);
int dwmac_phy_reset(void);
int dwmac_phy_link(unsigned *speed, int *full_duplex);
void dwmac_phy_dump(void);
void dwmac_phy_dump_ext(void);

/* dwmac_ring.c, diagnostics */
void dwmac_ring_dump(void);

/* Register access, all four blocks reached the same way. */
static inline uint32_t
dwmac_rd(vir_bytes block, unsigned off)
{
	return *(volatile uint32_t *)(block + off);
}

static inline void
dwmac_wr(vir_bytes block, unsigned off, uint32_t val)
{
	*(volatile uint32_t *)(block + off) = val;
}

#endif /* _DWMAC_H */
