#ifndef _XHCIREG_H
#define _XHCIREG_H

/*
 * Three register files, and it matters which is which.
 *
 *   the controller's own space:  xHCI from +0x0000, DWC3 from +0xc100
 *   the Rockchip glue:           CRU, PMUCRU, PMU - clocks, resets, power
 *   the PHY:                     analogue at its node's address, logical
 *                                in a separate syscon it points at
 *
 * The xHCI half is from the specification (xHCI 1.1, publicly published).
 * The DWC3 offsets are the ones every DWC3 driver uses - the global block
 * begins at +0xc100 - and the glue is from the vendor kernel of the exact
 * version running on the board (6.1.115); what was taken is register
 * numbers and field widths, which are facts about the silicon.  The
 * arithmetic worked out for this board, and the values read back off it,
 * are in port/cb2-usb/registers.md.
 */

/*===========================================================================*
 *    xHCI: capability registers                                             *
 *===========================================================================*/
#define XHCI_CAPLENGTH		0x00	/* 7:0 length, 31:16 HCIVERSION */
#define XHCI_HCSPARAMS1		0x04
#define XHCI_HCSPARAMS2		0x08
#define XHCI_HCSPARAMS3		0x0c
#define XHCI_HCCPARAMS1		0x10
#define XHCI_DBOFF		0x14
#define XHCI_RTSOFF		0x18
#define XHCI_HCCPARAMS2		0x1c

#define XHCI_HCS1_MAXSLOTS(v)	((v) & 0xff)
#define XHCI_HCS1_MAXINTRS(v)	(((v) >> 8) & 0x7ff)
#define XHCI_HCS1_MAXPORTS(v)	(((v) >> 24) & 0xff)

/*
 * The scratchpad the controller wants for itself is one number in two
 * fields, high bits and low bits far apart in the same register, and the
 * controller will not run without the buffers it asks for.
 */
#define XHCI_HCS2_IST(v)	((v) & 0xf)
#define XHCI_HCS2_ERST_MAX(v)	(((v) >> 4) & 0xf)
#define XHCI_HCS2_SPR(v)	(((v) >> 26) & 1)
#define XHCI_HCS2_MAX_SCRATCHPAD(v) \
    ((((v) >> 27) & 0x1f) | ((((v) >> 21) & 0x1f) << 5))

#define XHCI_HCC1_AC64(v)	((v) & 1)		/* 64-bit addressing */
#define XHCI_HCC1_CSZ(v)	(((v) >> 2) & 1)	/* context 64 bytes */
#define XHCI_HCC1_PPC(v)	(((v) >> 3) & 1)	/* port power control */
#define XHCI_HCC1_MAXPSA(v)	(((v) >> 12) & 0xf)
#define XHCI_HCC1_XECP(v)	((((v) >> 16) & 0xffff) * 4)

/*===========================================================================*
 *    xHCI: operational registers, at +CAPLENGTH                             *
 *===========================================================================*/
#define XHCI_USBCMD		0x00
#define XHCI_USBSTS		0x04
#define XHCI_PAGESIZE		0x08
#define XHCI_DNCTRL		0x14
#define XHCI_CRCR		0x18
#define XHCI_DCBAAP		0x30
#define XHCI_CONFIG		0x38
#define XHCI_PORTSC(n)		(0x400 + (n) * 0x10)	/* n from 0 */

#define XHCI_USBCMD_RS		(1 << 0)		/* run/stop */
#define XHCI_USBCMD_HCRST	(1 << 1)
#define XHCI_USBCMD_INTE	(1 << 2)
#define XHCI_USBCMD_HSEE	(1 << 3)

#define XHCI_USBSTS_HCH		(1 << 0)		/* halted */
#define XHCI_USBSTS_HSE		(1 << 2)
#define XHCI_USBSTS_EINT	(1 << 3)
#define XHCI_USBSTS_PCD		(1 << 4)
#define XHCI_USBSTS_CNR		(1 << 11)		/* not ready */

#define XHCI_PORTSC_CCS		(1 << 0)		/* device attached */
#define XHCI_PORTSC_PED		(1 << 1)		/* port enabled */
#define XHCI_PORTSC_PR		(1 << 4)		/* reset in progress */
#define XHCI_PORTSC_PLS(v)	(((v) >> 5) & 0xf)
#define XHCI_PORTSC_PP		(1 << 9)		/* port power */
#define XHCI_PORTSC_SPEED(v)	(((v) >> 10) & 0xf)

/* Extended capabilities: the map of port to protocol lives only here. */
#define XHCI_ECP_ID(v)		((v) & 0xff)
#define XHCI_ECP_NEXT(v)	((((v) >> 8) & 0xff) * 4)
#define XHCI_ECP_ID_LEGACY	1
#define XHCI_ECP_ID_PROTOCOL	2
#define XHCI_ECP_PROTO_MINOR(v)	(((v) >> 16) & 0xff)
#define XHCI_ECP_PROTO_MAJOR(v)	(((v) >> 24) & 0xff)
#define XHCI_ECP_PORT_OFF(v)	((v) & 0xff)
#define XHCI_ECP_PORT_COUNT(v)	(((v) >> 8) & 0xff)

/*===========================================================================*
 *    xHCI: runtime registers, at +RTSOFF                                    *
 *===========================================================================*/
#define XHCI_MFINDEX		0x00
#define XHCI_IR(n)		(0x20 + (n) * 0x20)	/* interrupter n */
#define XHCI_IR_IMAN		0x00
#define XHCI_IR_IMOD		0x04
#define XHCI_IR_ERSTSZ		0x08
#define XHCI_IR_ERSTBA		0x10
#define XHCI_IR_ERDP		0x18

#define XHCI_IMAN_IP		(1 << 0)	/* interrupt pending, w1c */
#define XHCI_IMAN_IE		(1 << 1)	/* interrupt enable */

/*
 * The dequeue pointer carries two things in its low bits: which segment of
 * the event ring the driver is in, and a write-one-to-clear flag saying it
 * has finished handling what it read.  Writing the pointer without the flag
 * leaves the controller believing the handler is still busy.
 */
#define XHCI_ERDP_DESI_MASK	0x7
#define XHCI_ERDP_EHB		(1 << 3)	/* event handler busy, w1c */

/*===========================================================================*
 *    xHCI: doorbells, at +DBOFF                                             *
 *===========================================================================*/
#define XHCI_DB(slot)		((slot) * 4)
#define XHCI_DB_CMD		0		/* slot 0 is the command ring */

/*===========================================================================*
 *    xHCI: transfer request blocks                                          *
 *===========================================================================*/
#define XHCI_TRB_SIZE		16

#define XHCI_TRB_C		(1u << 0)	/* cycle bit */
#define XHCI_TRB_TC		(1u << 1)	/* toggle cycle, on a link */
#define XHCI_TRB_TYPE_SHIFT	10
#define XHCI_TRB_TYPE(t)	((uint32_t)(t) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_TYPE_OF(c)	(((c) >> XHCI_TRB_TYPE_SHIFT) & 0x3f)

/* The types this milestone uses; the rest arrive with enumeration. */
#define XHCI_TRB_LINK		6
#define XHCI_TRB_ENABLE_SLOT	9
#define XHCI_TRB_ADDRESS_DEVICE	11
#define XHCI_TRB_CONFIGURE_EP	12
#define XHCI_TRB_NOOP_CMD	23
#define XHCI_TRB_TRANSFER_EVENT	32
#define XHCI_TRB_CMD_COMPLETION	33
#define XHCI_TRB_PORT_STATUS	34

/* Completion codes, of which only the first means anything went right. */
#define XHCI_CC_OF(status)	(((status) >> 24) & 0xff)
#define XHCI_CC_SUCCESS		1
#define XHCI_CC_TRB_ERROR	5
#define XHCI_CC_PARAMETER_ERROR	17

/*
 * The port a Port Status Change Event is about.  It is in the FIRST word of
 * the event, not the second: the first version of this driver read the
 * second and reported "port 0 changed" for a change on port 1.  Harmless
 * there because only the message used it, and exactly the kind of thing
 * that is not harmless once something acts on the number.
 */
#define XHCI_EVENT_PORT_ID(p0)	(((p0) >> 24) & 0xff)

/*===========================================================================*
 *    xHCI: the operational registers this milestone writes                  *
 *===========================================================================*/
#define XHCI_CRCR_RCS		(1u << 0)	/* ring cycle state */
#define XHCI_CRCR_CS		(1u << 1)	/* command stop */
#define XHCI_CRCR_CA		(1u << 2)	/* command abort */
#define XHCI_CRCR_CRR		(1u << 3)	/* command ring running */

#define XHCI_CONFIG_MAXSLOTS_MASK 0xff

/*
 * PORTSC is a minefield: seven of its bits are write-one-to-clear status,
 * and one - port enabled - is write-one-to-DISABLE.  So every write to it
 * goes through a mask that clears all eight, or the act of asking for a
 * port reset turns the port off and acknowledges changes nobody has looked
 * at yet.
 */
#define XHCI_PORTSC_PED_W1C	(1u << 1)
#define XHCI_PORTSC_WPR		(1u << 31)	/* warm reset, USB3 only */
#define XHCI_PORTSC_CSC		(1u << 17)
#define XHCI_PORTSC_PEC		(1u << 18)
#define XHCI_PORTSC_WRC		(1u << 19)
#define XHCI_PORTSC_OCC		(1u << 20)
#define XHCI_PORTSC_PRC		(1u << 21)
#define XHCI_PORTSC_PLC		(1u << 22)
#define XHCI_PORTSC_CEC		(1u << 23)
#define XHCI_PORTSC_CHANGES	(XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | \
    XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | \
    XHCI_PORTSC_PLC | XHCI_PORTSC_CEC)
#define XHCI_PORTSC_KEEP(v) \
    ((v) & ~(XHCI_PORTSC_CHANGES | XHCI_PORTSC_PED_W1C | XHCI_PORTSC_PR))

/* Link states worth naming. */
#define XHCI_PLS_U0		0
#define XHCI_PLS_DISABLED	4
#define XHCI_PLS_RXDETECT	5
#define XHCI_PLS_POLLING	7

/*
 * What a command completion or transfer event says about itself, besides
 * its completion code: which slot it is about, and how much of the
 * transfer did not happen.
 */
#define XHCI_EVENT_SLOT_ID(c)	(((c) >> 24) & 0xff)
#define XHCI_EVENT_EP_ID(c)	(((c) >> 16) & 0x1f)
#define XHCI_EVENT_LENGTH(s)	((s) & 0xffffff)

/*===========================================================================*
 *    xHCI: the TRBs a control transfer is made of                           *
 *===========================================================================*/
#define XHCI_TRB_NORMAL		1
#define XHCI_TRB_SETUP		2
#define XHCI_TRB_DATA		3
#define XHCI_TRB_STATUS		4

#define XHCI_TRB_ISP		(1u << 2)	/* interrupt on short packet */
#define XHCI_TRB_CH		(1u << 4)	/* chained to the next TRB */
#define XHCI_TRB_IOC		(1u << 5)	/* interrupt on completion */
#define XHCI_TRB_IDT		(1u << 6)	/* immediate data, setup */
#define XHCI_TRB_DIR_IN		(1u << 16)	/* on data and status */
#define XHCI_TRB_TRT_NONE	(0u << 16)	/* on setup: no data stage */
#define XHCI_TRB_TRT_OUT	(2u << 16)
#define XHCI_TRB_TRT_IN		(3u << 16)

/*===========================================================================*
 *    xHCI: slot and endpoint contexts                                       *
 *===========================================================================*/
/*
 * A context is 32 or 64 bytes depending on CSZ, which on this part is 64.
 * Everything below counts in words within a context, and the stride
 * between contexts comes from xhci.context_size - reading a 64-byte
 * layout as 32 does not fail, it reads the next endpoint's fields.
 */
#define XHCI_CTX_WORDS		8		/* words this code writes */

/* Slot context, word 0: route string, speed, number of contexts. */
#define XHCI_SLOT_ROUTE(r)	((r) & 0xfffff)
#define XHCI_SLOT_SPEED(s)	(((s) & 0xf) << 20)
#define XHCI_SLOT_ENTRIES(n)	(((n) & 0x1f) << 27)
/* Slot context, word 1: which root hub port this device hangs off. */
#define XHCI_SLOT_RHPORT(p)	(((p) & 0xff) << 16)
/*
 * Slot context, word 2: the transaction translator, through which a low-
 * or full-speed device behind a high-speed hub is reached.  Which hub,
 * and which of its ports.
 */
#define XHCI_SLOT_TT_SLOT(s)	((s) & 0xff)
#define XHCI_SLOT_TT_PORT(p)	(((p) & 0xff) << 8)

/* Endpoint context, word 1: error count, type, maximum packet size. */
#define XHCI_EP_CERR(n)		(((n) & 0x3) << 1)
#define XHCI_EP_TYPE(t)		(((t) & 0x7) << 3)
#define XHCI_EP_MAXBURST(n)	(((n) & 0xff) << 8)
#define XHCI_EP_MAXPACKET(n)	(((n) & 0xffff) << 16)

#define XHCI_EP_TYPE_BULK_OUT	2
#define XHCI_EP_TYPE_INTR_OUT	3
#define XHCI_EP_TYPE_CONTROL	4
#define XHCI_EP_TYPE_BULK_IN	6
#define XHCI_EP_TYPE_INTR_IN	7

/* Endpoint context, word 0: how often an interrupt endpoint is polled. */
#define XHCI_EP_INTERVAL(n)	(((n) & 0xff) << 16)

/* Endpoint context, word 2: where its transfer ring starts, and its cycle. */
#define XHCI_EP_DCS		(1u << 0)
/* Endpoint context, word 4: how long an average transfer is. */
#define XHCI_EP_AVG_TRB(n)	((n) & 0xffff)

/* Input control context: which contexts of the input this command means. */
#define XHCI_INPUT_ADD_SLOT	(1u << 0)
#define XHCI_INPUT_ADD_EP0	(1u << 1)

/* The doorbell target that means "the control endpoint of this slot". */
#define XHCI_DB_EP0		1

/* Address Device, control word: the slot, and whether to skip SET_ADDRESS. */
#define XHCI_TRB_SLOT_ID(s)	(((s) & 0xffu) << 24)
#define XHCI_TRB_BSR		(1u << 9)

/*===========================================================================*
 *    USB itself: the few requests enumeration needs                         *
 *===========================================================================*/
#define USB_REQ_DIR_IN		0x80
#define USB_REQ_GET_DESCRIPTOR	6
#define USB_DESC_DEVICE		1
#define USB_DESC_CONFIG		2
#define USB_DESC_STRING		3

/* The device descriptor, as it arrives on the wire: little-endian, packed. */
struct usb_device_descriptor {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
} __attribute__((packed));

/*===========================================================================*
 *    DWC3: the glue layer that stands in front of the xHCI                  *
 *===========================================================================*/
#define DWC3_GSBUSCFG0		0xc100
#define DWC3_GSBUSCFG1		0xc104
#define DWC3_GCTL		0xc110
#define DWC3_GSTS		0xc118
#define DWC3_GSNPSID		0xc120
#define DWC3_GGPIO		0xc124
#define DWC3_GUID		0xc128
#define DWC3_GUCTL		0xc12c
#define DWC3_GHWPARAMS0		0xc140
#define DWC3_GHWPARAMS1		0xc144
#define DWC3_GUSB2PHYCFG0	0xc200
#define DWC3_GUSB3PIPECTL0	0xc2c0
#define DWC3_GFLADJ		0xc600

#define DWC3_GSNPSID_MASK	0xffff0000
#define DWC3_GSNPSID_DWC3	0x55330000

#define DWC3_GCTL_CORESOFTRESET	(1 << 11)
#define DWC3_GCTL_PRTCAPDIR_SHIFT 12
#define DWC3_GCTL_PRTCAPDIR_MASK 0x3
#define DWC3_GCTL_PRTCAPDIR_HOST 1
#define DWC3_GCTL_PRTCAPDIR_DEVICE 2
#define DWC3_GCTL_SCALEDOWN_MASK (0x3 << 4)
#define DWC3_GCTL_DISSCRAMBLE	(1 << 3)
#define DWC3_GCTL_U2EXIT_LFPS	(1 << 2)

#define DWC3_GUSB2PHYCFG_PHYSOFTRST (1u << 31)
#define DWC3_GUSB2PHYCFG_SUSPHY	(1 << 6)
#define DWC3_GUSB2PHYCFG_ENBLSLPM (1 << 8)
#define DWC3_GUSB2PHYCFG_PHYIF	(1 << 3)	/* 1: UTMI+ 16 bit */
#define DWC3_GUSB2PHYCFG_U2FREECLK_EXISTS (1 << 30)

#define DWC3_GUSB3PIPECTL_PHYSOFTRST (1u << 31)

/*===========================================================================*
 *    Rockchip: clocks and resets (CRU)                                      *
 *===========================================================================*/
#define RK3568_CRU_MODE_CON0	0x00c0
#define RK3568_CRU_CLKSEL_CON(x) (0x0100 + (x) * 4)
#define RK3568_CRU_CLKGATE_CON(x) (0x0300 + (x) * 4)
#define RK3568_CRU_SOFTRST_CON(x) (0x0400 + (x) * 4)
#define RK_SOFTRST_PER_REG	16

/*
 * The 480 MHz clock USB 2.0 runs on is generated by the PHY, and the CRU
 * has to be told to look at it rather than at the crystal.  This is the
 * one write in the whole sequence that cannot be worked out from any
 * controller register: nothing reflects it, and with the crystal selected
 * the controller comes up and sees no device at all.
 */
#define RK3568_MODE_USB480M_SHIFT 14
#define RK3568_MODE_USB480M_MASK 0x3
#define RK3568_MODE_USB480M_PHY	1

/* The PD_PIPE clocks, which are what both xHCIs run on. */
#define RK3568_CLKGATE_PIPE	10
#define RK3568_GATE_ACLK_PIPE	(1 << 0)
#define RK3568_GATE_PCLK_PIPE	(1 << 1)
#define RK3568_GATE_ACLK_USB3OTG0 (1 << 8)
#define RK3568_GATE_CLK_USB3OTG0_REF (1 << 9)
#define RK3568_GATE_CLK_USB3OTG0_SUSPEND (1 << 10)
#define RK3568_GATE_ACLK_USB3OTG1 (1 << 12)
#define RK3568_GATE_CLK_USB3OTG1_REF (1 << 13)
#define RK3568_GATE_CLK_USB3OTG1_SUSPEND (1 << 14)

/* Where the suspend clock comes from: clear means the 24 MHz crystal. */
#define RK3568_CLKSEL_PIPE	29
#define RK3568_USB3OTG0_SUSPEND_SRC_BIT 8
#define RK3568_USB3OTG1_SUSPEND_SRC_BIT 9

/*===========================================================================*
 *    Rockchip: the PMU's own clock controller (PMUCRU)                      *
 *===========================================================================*/
#define RK3568_PMUCRU_CLKSEL_CON(x) (0x0100 + (x) * 4)
#define RK3568_PMUCRU_CLKGATE_CON(x) (0x0180 + (x) * 4)

#define RK3568_PMU_CLKGATE_USB	2
#define RK3568_PMU_GATE_CLK_REF24M (1 << 0)
#define RK3568_PMU_GATE_XIN_USBPHY0 (1 << 1)
#define RK3568_PMU_GATE_XIN_USBPHY1 (1 << 2)

#define RK3568_PMU_CLKSEL_USBPHY 8	/* bit 0 phy0, bit 1 phy1 */

/*===========================================================================*
 *    Rockchip: the power domain (PMU)                                       *
 *===========================================================================*/
#define RK3568_PMU_PWR_CON	0x00a0
#define RK3568_PMU_PWR_STATUS	0x0098
#define RK3568_PMU_IDLE_REQ	0x0050
#define RK3568_PMU_IDLE_ACK	0x0060
#define RK3568_PMU_IDLE_ST	0x0068

/*===========================================================================*
 *    Rockchip: the PHY, logical half (the "usbgrf" syscon)                  *
 *===========================================================================*/
#define RK_USBGRF_OTG_CON0	0x0000
#define RK_USBGRF_HOST_CON0	0x0004
#define RK_USBGRF_CON2		0x0008
#define RK_USBGRF_CON3		0x000c
#define RK_USBGRF_LS_FILTER_CON	0x0040
#define RK_USBGRF_BVALID_FILTER	0x0048
#define RK_USBGRF_ID_FILTER	0x004c
#define RK_USBGRF_DET_EN	0x0080
#define RK_USBGRF_DET_ST	0x0084
#define RK_USBGRF_DET_CLR	0x0088
#define RK_USBGRF_STATUS	0x00c0

/* phy_sus, bits 8:0 of the port's CON0. */
#define RK_PHY_SUS_MASK		0x1ff
#define RK_PHY_SUS_ON		0x000	/* the port runs */
#define RK_PHY_SUS_CTRL		0x1d2	/* suspend controlled by the host */
#define RK_PHY_SUS_OFF		0x1d1	/* the port is suspended */

#define RK_USBGRF_CLKOUT_CTL_BIT 4	/* 0: the 480 MHz output is on */

/* What the vendor writes into the filters: 10 ms at a 100 MHz pclk. */
#define RK_FILTER_COUNTER	0x000f4240
#define RK_LS_FILTER_VALUE	0x00030100
#define RK_LS_FILTER_MASK	0x000fffff

/*===========================================================================*
 *    Rockchip: the PHY, analogue half (the node's own address)              *
 *===========================================================================*/
#define RK_PHY_PORT_STRIDE	0x0400		/* OTG at 0, host at 0x400 */
#define RK_PHY_REG_PREEMPHASIS	0x0000		/* bits 2:0 */
#define RK_PHY_PREEMPHASIS_VAL	0x4
#define RK_PHY_REG_EYE		0x0030		/* bit 2, bits 6:4 */
#define RK_PHY_EYE_DIFFRCV_BIT	(1 << 2)
#define RK_PHY_EYE_HEIGHT_SHIFT	4
#define RK_PHY_EYE_HEIGHT_MASK	0x7
#define RK_PHY_EYE_HEIGHT_437MV	0x6

/*
 * Which of the SoC's two USB2 PHYs this is.  The vendor driver keys the
 * extra tuning off the PHY's own address, and so does this: it is a fact
 * about the part, in the same category as the register offsets above, and
 * not a board identifier.  Only PHY0 serves the two xHCIs, so in practice
 * this driver never reaches the other one.
 */
#define RK3568_USB2PHY0_BASE	0xfe8a0000
#define RK3568_USB2PHY1_BASE	0xfe8b0000

#endif /* _XHCIREG_H */
