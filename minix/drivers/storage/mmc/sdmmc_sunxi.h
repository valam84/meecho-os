/*
 * Allwinner's SD/MMC controller ("sunxi-mmc"): A20, A64, H3, H6, H616, H313.
 *
 * A descendant of the Synopsys DesignWare part sdmmc_dwmshc.c drives - the
 * command word, the interrupt bits and the DMA descriptors are the same - with
 * its own register layout, a divider that lives in the clock control register
 * and no separate enable register, and a "new timing mode" from the A64 on.
 * The names are NetBSD's (sys/arch/arm/sunxi/sunxi_mmc.h) and Linux's; the
 * register set QEMU emulates for the A64 variant (hw/sd/allwinner-sdhost.c)
 * agrees with both.
 */

#ifndef _SDMMC_SUNXI_H
#define _SDMMC_SUNXI_H

#define SX_GCTRL		0x000
#define  SX_GCTRL_SOFT_RESET	(1u << 0)
#define  SX_GCTRL_FIFO_RESET	(1u << 1)
#define  SX_GCTRL_DMA_RESET	(1u << 2)
#define  SX_GCTRL_INT_ENABLE	(1u << 4)
#define  SX_GCTRL_DMA_ENABLE	(1u << 5)
#define  SX_GCTRL_DDR		(1u << 10)
#define  SX_GCTRL_WAIT_MEM_DONE	(1u << 30)
#define  SX_GCTRL_AHB		(1u << 31)	/* FIFO by the processor */
#define  SX_GCTRL_ALL_RESET \
	(SX_GCTRL_SOFT_RESET | SX_GCTRL_FIFO_RESET | SX_GCTRL_DMA_RESET)
#define SX_CLKCR		0x004
#define  SX_CLKCR_DIV_MASK	0xff		/* card = module / 2N, 0: /1 */
#define  SX_CLKCR_ON		(1u << 16)
#define  SX_CLKCR_LOW_POWER	(1u << 17)
#define  SX_CLKCR_MASK_DATA0	(1u << 31)
#define SX_TMOUT		0x008
#define SX_WIDTH		0x00c
#define  SX_WIDTH_1		0
#define  SX_WIDTH_4		1
#define  SX_WIDTH_8		2
#define SX_BLKSZ		0x010
#define SX_BYTECNT		0x014
#define SX_CMD			0x018
#define  SX_CMD_INDEX(n)	((uint32_t)(n) & 0x3f)
#define  SX_CMD_RESP_EXP	(1u << 6)
#define  SX_CMD_RESP_LONG	(1u << 7)
#define  SX_CMD_RESP_CRC	(1u << 8)
#define  SX_CMD_DATA_EXP	(1u << 9)
#define  SX_CMD_WRITE		(1u << 10)
#define  SX_CMD_SEND_STOP	(1u << 12)
#define  SX_CMD_PRV_DAT_WAIT	(1u << 13)
#define  SX_CMD_STOP_ABORT	(1u << 14)
#define  SX_CMD_INIT		(1u << 15)
#define  SX_CMD_UPD_CLK		(1u << 21)
#define  SX_CMD_START		(1u << 31)
#define SX_ARG			0x01c
#define SX_RESP0		0x020
#define SX_IMASK		0x030
#define SX_MINT			0x034
#define SX_RINT			0x038
#define  SX_INT_RE		(1u << 1)
#define  SX_INT_CMD_DONE	(1u << 2)
#define  SX_INT_DTO		(1u << 3)
#define  SX_INT_TXDR		(1u << 4)
#define  SX_INT_RXDR		(1u << 5)
#define  SX_INT_RCRC		(1u << 6)
#define  SX_INT_DCRC		(1u << 7)
#define  SX_INT_RTO		(1u << 8)
#define  SX_INT_DRTO		(1u << 9)
#define  SX_INT_HTO		(1u << 10)
#define  SX_INT_FRUN		(1u << 11)
#define  SX_INT_HLE		(1u << 12)
#define  SX_INT_SBE		(1u << 13)
#define  SX_INT_ACD		(1u << 14)
#define  SX_INT_EBE		(1u << 15)
#define  SX_INT_CMD_ERROR \
	(SX_INT_RE | SX_INT_RCRC | SX_INT_RTO | SX_INT_HLE)
#define  SX_INT_DATA_ERROR \
	(SX_INT_DCRC | SX_INT_DRTO | SX_INT_HTO | SX_INT_FRUN | \
	 SX_INT_SBE | SX_INT_EBE)
#define  SX_INT_ALL		0xffffffffu
#define SX_STATUS		0x03c
#define  SX_STATUS_FIFO_EMPTY	(1u << 2)
#define  SX_STATUS_FIFO_FULL	(1u << 3)
#define  SX_STATUS_DATA_BUSY	(1u << 9)
#define SX_FTRGL		0x040
#define  SX_FTRGL_DEFAULT	0x20070008	/* burst 8, rx 7, tx 8 */
#define SX_FUNS			0x044
#define SX_A12A			0x058
#define SX_NTSR			0x05c
#define  SX_NTSR_NEW_MODE	(1u << 31)
#define SX_DMAC			0x080
#define  SX_DMAC_SOFT_RESET	(1u << 0)
#define  SX_DMAC_FIX_BURST	(1u << 1)
#define  SX_DMAC_IDMA_ON	(1u << 7)
#define SX_DLBA			0x084
#define SX_IDST			0x088
#define  SX_IDST_TX		(1u << 0)
#define  SX_IDST_RX		(1u << 1)
#define  SX_IDST_FBE		(1u << 2)
#define  SX_IDST_DU		(1u << 4)
#define  SX_IDST_CES		(1u << 5)
#define  SX_IDST_NIS		(1u << 8)
#define  SX_IDST_AIS		(1u << 9)
#define  SX_IDST_ERROR \
	(SX_IDST_FBE | SX_IDST_DU | SX_IDST_CES | SX_IDST_AIS)
#define  SX_IDST_ALL		0x337
#define SX_IDIE			0x08c
#define SX_SAMP_DL		0x144
#define  SX_SAMP_DL_SW_EN	(1u << 7)

/* Where the data FIFO is: 0x100 on the A10/A13, 0x200 from the A31 on. */
#define SX_FIFO_OLD		0x100
#define SX_FIFO			0x200

/*
 * One DMA descriptor: the same four words as the DesignWare engine's. The
 * buffer length field is 13 or 16 bits by variant; 4096 bytes per descriptor
 * fits either. On the A100/H616 family the two address words - and the list
 * base in DLBA - hold the address divided by four (Linux's idma_des_shift),
 * which reaches 16 GiB with 32 bits.
 */
struct sx_desc {
	uint32_t	config;
	uint32_t	size;
	uint32_t	buf;
	uint32_t	next;
};

#define SX_DES_DIC		(1u << 1)
#define SX_DES_LD		(1u << 2)
#define SX_DES_FD		(1u << 3)
#define SX_DES_CH		(1u << 4)
#define SX_DES_ER		(1u << 5)
#define SX_DES_CES		(1u << 30)
#define SX_DES_OWN		(1u << 31)
#define SX_DESC_MAX_LEN		4096

#endif /* _SDMMC_SUNXI_H */
