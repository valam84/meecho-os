/*
 * Registers of the DWC3 core and of Qualcomm's wrapper around it, as far
 * as this driver reads them.  Offsets and fields: Linux
 * drivers/usb/dwc3/core.h and dwc3-qcom.c (port/clover/hw-net.md, §3).
 */
#ifndef _DWC3_REGS_H
#define _DWC3_REGS_H

/* The core's global and device registers, from its reg base. */
#define DWC3_GCTL		0xc110
#define DWC3_GCTL_PRTCAP(v)	(((v) >> 12) & 3)	/* 1 host, 2 device */
#define DWC3_GSTS		0xc118
#define DWC3_GSNPSID		0xc120
#define DWC3_GSNPSID_MASK	0xffff0000u
#define DWC3_GSNPSID_DWC3	0x55330000u
#define DWC3_GHWPARAMS3		0xc14c
#define DWC3_GUSB2PHYCFG	0xc200
#define DWC3_GEVNTADRLO		0xc400
#define DWC3_GEVNTSIZ		0xc408
#define DWC3_DCFG		0xc700
#define DWC3_DCTL		0xc704
#define DWC3_DCTL_RUN_STOP	0x80000000u
#define DWC3_DSTS		0xc70c
#define DWC3_DSTS_DEVCTRLHLT	0x00400000u

/* Qualcomm's wrapper (QSCRATCH), from the wrapper node's reg. */
#define QSCRATCH_GENERAL_CFG	0x08
#define QSCRATCH_HS_PHY_CTRL	0x10
#define QSCRATCH_SS_PHY_CTRL	0x30

/* The register the core is told it may be read from: the end of DSTS. */
#define DWC3_CORE_MIN_SIZE	(DWC3_DSTS + 4)

#endif /* _DWC3_REGS_H */
