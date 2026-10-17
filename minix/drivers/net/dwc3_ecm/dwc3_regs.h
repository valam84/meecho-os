/*
 * Registers of the DWC3 core and of Qualcomm's wrapper around it, as far
 * as this driver uses them.  Offsets and fields: Linux
 * drivers/usb/dwc3/core.h, gadget.h and dwc3-qcom.c
 * (port/clover/hw-net.md, §3).
 */
#ifndef _DWC3_REGS_H
#define _DWC3_REGS_H

/* Global registers, from the core's reg base. */
#define DWC3_GCTL		0xc110
#define DWC3_GCTL_CORESOFTRESET	(1u << 11)
#define DWC3_GCTL_PRTCAP(v)	(((v) >> 12) & 3)	/* 1 host, 2 device */
#define DWC3_GCTL_PRTCAPDIR_MASK (3u << 12)
#define DWC3_GCTL_PRTCAP_DEVICE	2u
#define DWC3_GSTS		0xc118
#define DWC3_GSNPSID		0xc120
#define DWC3_GSNPSID_MASK	0xffff0000u
#define DWC3_GSNPSID_DWC3	0x55330000u
#define DWC3_GHWPARAMS3		0xc14c
#define DWC3_GUSB2PHYCFG	0xc200
#define DWC3_GUSB2PHYCFG_SUSPHY	(1u << 6)
#define DWC3_GUSB2PHYCFG_ENBLSLPM (1u << 8)
#define DWC3_GEVNTADRLO		0xc400
#define DWC3_GEVNTADRHI		0xc404
#define DWC3_GEVNTSIZ		0xc408
#define DWC3_GEVNTSIZ_INTMASK	0x80000000u
#define DWC3_GEVNTCOUNT		0xc40c

/* Device registers. */
#define DWC3_DCFG		0xc700
#define DWC3_DCFG_SPEED_MASK	7u
#define DWC3_DCFG_HIGHSPEED	0u
#define DWC3_DCFG_DEVADDR_MASK	(0x7fu << 3)
#define DWC3_DCFG_DEVADDR(a)	((uint32_t)(a) << 3)
#define DWC3_DCFG_LPM_CAP	(1u << 22)
#define DWC3_DCFG_IGNSTRMPP	(1u << 23)
#define DWC3_DCTL		0xc704
#define DWC3_DCTL_RUN_STOP	0x80000000u
#define DWC3_DCTL_CSFTRST	(1u << 30)
#define DWC3_DCTL_KEEP_CONNECT	(1u << 19)
#define DWC3_DCTL_TSTCTRL_MASK	(0xfu << 1)
#define DWC3_DEVTEN		0xc708
#define DWC3_DEVTEN_DISCONNEVTEN (1u << 0)
#define DWC3_DEVTEN_USBRSTEN	(1u << 1)
#define DWC3_DEVTEN_CONNECTDONEEN (1u << 2)
#define DWC3_DEVTEN_WKUPEVTEN	(1u << 4)
#define DWC3_DEVTEN_U3L2L1SUSPEN (1u << 6)
#define DWC3_DEVTEN_ERRTICERREN	(1u << 9)
#define DWC3_DEVTEN_CMDCMPLTEN	(1u << 10)
#define DWC3_DEVTEN_EVNTOVERFLOWEN (1u << 11)
#define DWC3_DSTS		0xc70c
#define DWC3_DSTS_CONNECTSPD	7u		/* 0 high, 1 full speed */
#define DWC3_DSTS_DEVCTRLHLT	0x00400000u
#define DWC3_DALEPENA		0xc720

/* Commands of physical endpoint n (0 is ep0 OUT, 1 ep0 IN). */
#define DWC3_DEP_BASE(n)	(0xc800 + (n) * 0x10)
#define DWC3_DEPCMDPAR2(n)	(DWC3_DEP_BASE(n) + 0x0)
#define DWC3_DEPCMDPAR1(n)	(DWC3_DEP_BASE(n) + 0x4)
#define DWC3_DEPCMDPAR0(n)	(DWC3_DEP_BASE(n) + 0x8)
#define DWC3_DEPCMD(n)		(DWC3_DEP_BASE(n) + 0xc)
#define DWC3_DEPCMD_CMDACT	(1u << 10)
#define DWC3_DEPCMD_CMDIOC	(1u << 8)
#define DWC3_DEPCMD_STATUS(v)	(((v) >> 12) & 0xf)
#define DWC3_DEPCMD_PARAM(x)	((uint32_t)(x) << 16)
#define DWC3_DEPCMD_RSC_IDX(v)	(((v) >> 16) & 0x7f)
#define DWC3_DEPCMD_SETEPCONFIG	1u
#define DWC3_DEPCMD_SETTRANSFRESOURCE 2u
#define DWC3_DEPCMD_SETSTALL	4u
#define DWC3_DEPCMD_STARTTRANSFER 6u
#define DWC3_DEPCMD_ENDTRANSFER	8u
#define DWC3_DEPCMD_DEPSTARTCFG	9u

/* SETEPCONFIG parameters (gadget.h). */
#define DWC3_DEPCFG_EP_TYPE(n)	(((uint32_t)(n) & 3) << 1)
#define DWC3_DEPCFG_MPS(n)	(((uint32_t)(n) & 0x7ff) << 3)
#define DWC3_DEPCFG_FIFO_NUMBER(n) (((uint32_t)(n) & 0x1f) << 17)
#define DWC3_DEPCFG_ACTION_INIT	(0u << 30)
#define DWC3_DEPCFG_ACTION_MODIFY (2u << 30)
#define DWC3_DEPCFG_XFER_COMPLETE_EN (1u << 8)
#define DWC3_DEPCFG_XFER_NOT_READY_EN (1u << 10)
#define DWC3_DEPCFG_BINTERVAL_M1(n) (((uint32_t)(n) & 0xff) << 16)
#define DWC3_DEPCFG_EP_NUMBER(n) (((uint32_t)(n) & 0x1f) << 25)
#define DWC3_DEPXFERCFG_NUM_XFER_RES(n)	((uint32_t)(n) & 0xffff)

/* A TRB: buffer address, size and status, control. */
#define DWC3_TRB_SIZE_MASK	0x00ffffffu
#define DWC3_TRB_STS(v)		(((v) >> 28) & 0xf)
#define DWC3_TRB_CTRL_HWO	(1u << 0)
#define DWC3_TRB_CTRL_LST	(1u << 1)
#define DWC3_TRB_CTRL_TRBCTL(n)	((uint32_t)(n) << 4)
#define DWC3_TRB_CTRL_ISP_IMI	(1u << 10)
#define DWC3_TRB_CTRL_IOC	(1u << 11)
#define DWC3_TRBCTL_SETUP	DWC3_TRB_CTRL_TRBCTL(2)
#define DWC3_TRBCTL_STATUS2	DWC3_TRB_CTRL_TRBCTL(3)
#define DWC3_TRBCTL_STATUS3	DWC3_TRB_CTRL_TRBCTL(4)
#define DWC3_TRBCTL_DATA	DWC3_TRB_CTRL_TRBCTL(5)
#define DWC3_TRBCTL_NORMAL	DWC3_TRB_CTRL_TRBCTL(1)

/*
 * Events, one 32-bit word each.  Bit 0 clear: an endpoint event - its
 * physical endpoint (5:1), event (9:6), status (15:12), parameters
 * (31:16).  Bit 0 set and bits 7:1 zero: a device event - its type
 * (11:8) and information (24:16).
 */
#define DWC3_EV_IS_DEV(e)	((e) & 1)
#define DWC3_EV_DEV_CLASS(e)	(((e) >> 1) & 0x7f)
#define DWC3_EV_DEV_TYPE(e)	(((e) >> 8) & 0xf)
#define DWC3_EV_DEV_INFO(e)	(((e) >> 16) & 0x1ff)
#define DWC3_EV_EP(e)		(((e) >> 1) & 0x1f)
#define DWC3_EV_EP_TYPE(e)	(((e) >> 6) & 0xf)
#define DWC3_EV_EP_STATUS(e)	(((e) >> 12) & 0xf)
#define DWC3_DEVT_DISCONNECT	0
#define DWC3_DEVT_RESET		1
#define DWC3_DEVT_CONNECT_DONE	2
#define DWC3_DEVT_LINK_STATUS	3
#define DWC3_DEVT_WAKEUP	4
#define DWC3_DEVT_SUSPEND	6
#define DWC3_DEVT_ERRATIC	9
#define DWC3_DEVT_CMD_CMPL	10
#define DWC3_DEVT_OVERFLOW	11
#define DWC3_DEPEVT_XFERCOMPLETE 1
#define DWC3_DEPEVT_XFERNOTREADY 3
#define DWC3_DEPEVT_EPCMDCMPLT	7
#define DWC3_DEPEVT_CONTROL_DATA 1	/* XferNotReady on ep0/ep1 */
#define DWC3_DEPEVT_CONTROL_STATUS 2

/* Qualcomm's wrapper (QSCRATCH), from the wrapper node's reg. */
#define QSCRATCH_GENERAL_CFG	0x08
#define QSCRATCH_HS_PHY_CTRL	0x10
#define QSCRATCH_UTMI_OTG_VBUS_VALID (1u << 20)
#define QSCRATCH_SW_SESSVLD_SEL	(1u << 28)
#define QSCRATCH_SS_PHY_CTRL	0x30
#define QSCRATCH_LANE0_PWR_PRESENT (1u << 24)

/* The register the core is told it may be read from: the end of DSTS. */
#define DWC3_CORE_MIN_SIZE	(DWC3_DEP_BASE(2))

#endif /* _DWC3_REGS_H */
