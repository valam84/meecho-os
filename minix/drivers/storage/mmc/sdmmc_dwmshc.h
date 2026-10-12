/*
 * The Synopsys DesignWare Mobile Storage Host Controller ("dw-mshc").
 *
 * Not the SD Host Controller standard, and not to be confused with the
 * DesignWare Cores part sdmmc_sdhci.c drives: this is Synopsys's own register
 * set, older than both, and found on Rockchip, Samsung Exynos, HiSilicon,
 * Altera/Intel SoCFPGA and StarFive parts. The names are the databook's, as
 * NetBSD's dwc_mmc_reg.h has them; the offsets were checked against the
 * board, where the vendor system's controller reads back as listed in
 * port/cb2-sd/linux-reference.txt (VERID 0x270a, HCON 0x04c434c1).
 */

#ifndef _SDMMC_DWMSHC_H
#define _SDMMC_DWMSHC_H

#define DW_CTRL			0x000
#define  DW_CTRL_RESET		(1u << 0)	/* controller */
#define  DW_CTRL_FIFO_RESET	(1u << 1)
#define  DW_CTRL_DMA_RESET	(1u << 2)
#define  DW_CTRL_INT_ENABLE	(1u << 4)
#define  DW_CTRL_DMA_ENABLE	(1u << 5)
#define  DW_CTRL_USE_IDMAC	(1u << 25)
#define  DW_CTRL_ALL_RESET \
	(DW_CTRL_RESET | DW_CTRL_FIFO_RESET | DW_CTRL_DMA_RESET)
#define DW_PWREN		0x004
#define DW_CLKDIV		0x008	/* divisor 0: 2*N, 0 = bypass */
#define DW_CLKSRC		0x00c
#define DW_CLKENA		0x010
#define  DW_CLKENA_ON		(1u << 0)
#define  DW_CLKENA_LOW_POWER	(1u << 16)
#define DW_TMOUT		0x014
#define DW_CTYPE		0x018
#define  DW_CTYPE_1BIT		0
#define  DW_CTYPE_4BIT		(1u << 0)
#define  DW_CTYPE_8BIT		(1u << 16)
#define DW_BLKSIZ		0x01c
#define DW_BYTCNT		0x020
#define DW_INTMASK		0x024
#define DW_CMDARG		0x028
#define DW_CMD			0x02c
#define  DW_CMD_INDEX(n)	((uint32_t)(n) & 0x3f)
#define  DW_CMD_RESP_EXP	(1u << 6)
#define  DW_CMD_RESP_LONG	(1u << 7)
#define  DW_CMD_RESP_CRC	(1u << 8)
#define  DW_CMD_DATA_EXP	(1u << 9)
#define  DW_CMD_WRITE		(1u << 10)
#define  DW_CMD_SEND_STOP	(1u << 12)
#define  DW_CMD_PRV_DAT_WAIT	(1u << 13)
#define  DW_CMD_STOP_ABORT	(1u << 14)
#define  DW_CMD_INIT		(1u << 15)
#define  DW_CMD_UPD_CLK		(1u << 21)
#define  DW_CMD_USE_HOLD_REG	(1u << 29)
#define  DW_CMD_START		(1u << 31)
#define DW_RESP0		0x030
#define DW_MINTSTS		0x040
#define DW_RINTSTS		0x044
#define  DW_INT_CD		(1u << 0)	/* card detect changed */
#define  DW_INT_RE		(1u << 1)	/* response error */
#define  DW_INT_CMD_DONE	(1u << 2)
#define  DW_INT_DTO		(1u << 3)	/* data transfer over */
#define  DW_INT_TXDR		(1u << 4)
#define  DW_INT_RXDR		(1u << 5)
#define  DW_INT_RCRC		(1u << 6)
#define  DW_INT_DCRC		(1u << 7)
#define  DW_INT_RTO		(1u << 8)	/* response timeout */
#define  DW_INT_DRTO		(1u << 9)	/* data read timeout */
#define  DW_INT_HTO		(1u << 10)	/* host starvation */
#define  DW_INT_FRUN		(1u << 11)	/* FIFO under/overrun */
#define  DW_INT_HLE		(1u << 12)	/* hardware locked write */
#define  DW_INT_SBE		(1u << 13)	/* start bit error */
#define  DW_INT_ACD		(1u << 14)	/* auto command done */
#define  DW_INT_EBE		(1u << 15)	/* end bit error */
#define  DW_INT_CMD_ERROR \
	(DW_INT_RE | DW_INT_RCRC | DW_INT_RTO | DW_INT_HLE)
#define  DW_INT_DATA_ERROR \
	(DW_INT_DCRC | DW_INT_DRTO | DW_INT_HTO | DW_INT_FRUN | \
	 DW_INT_SBE | DW_INT_EBE)
#define  DW_INT_ALL		0xffffffffu
#define DW_STATUS		0x048
#define  DW_STATUS_FIFO_EMPTY	(1u << 2)
#define  DW_STATUS_FIFO_FULL	(1u << 3)
#define  DW_STATUS_DATA_BUSY	(1u << 9)	/* the card holds DAT0 */
#define  DW_STATUS_FIFO_COUNT(s) (((s) >> 17) & 0x1fff)
#define DW_FIFOTH		0x04c
#define  DW_FIFOTH_MSIZE_SHIFT	28
#define  DW_FIFOTH_RX_SHIFT	16
#define  DW_FIFOTH_RX_MASK	0xfff
#define DW_CDETECT		0x050
#define  DW_CDETECT_ABSENT	(1u << 0)	/* card_detect_n: 1 = empty */
#define DW_WRTPRT		0x054
#define DW_TCBCNT		0x05c
#define DW_VERID		0x06c
#define DW_HCON			0x070
#define  DW_HCON_DMA_IF(h)	(((h) >> 16) & 0x3)	/* 0: internal */
#define  DW_HCON_ADDR64(h)	(((h) >> 27) & 0x1)
#define DW_UHS_REG		0x074
#define  DW_UHS_DDR		(1u << 16)
#define DW_RST_N		0x078
#define DW_BMOD			0x080
#define  DW_BMOD_SWR		(1u << 0)
#define  DW_BMOD_FB		(1u << 1)
#define  DW_BMOD_DE		(1u << 7)
#define DW_PLDMND		0x084
#define DW_DBADDR		0x088
#define DW_IDSTS		0x08c
#define  DW_IDSTS_TI		(1u << 0)
#define  DW_IDSTS_RI		(1u << 1)
#define  DW_IDSTS_FBE		(1u << 2)	/* fatal bus error */
#define  DW_IDSTS_DU		(1u << 4)	/* descriptor unavailable */
#define  DW_IDSTS_CES		(1u << 5)	/* card error summary */
#define  DW_IDSTS_NIS		(1u << 8)
#define  DW_IDSTS_AIS		(1u << 9)
#define  DW_IDSTS_ERROR \
	(DW_IDSTS_FBE | DW_IDSTS_DU | DW_IDSTS_CES | DW_IDSTS_AIS)
#define DW_IDINTEN		0x090
#define DW_DSCADDR		0x094
#define DW_BUFADDR		0x098
#define DW_CARDTHRCTL		0x100

/*
 * Where the data FIFO is: 0x100 before version 2.40a, 0x200 from it on.
 * The version is read, not assumed; this board's part is 2.70a.
 */
#define DW_DATA_OLD		0x100
#define DW_DATA_240A		0x200
#define DW_VERID_240A		0x240a

/*
 * One internal-DMA descriptor in chained mode, 32-bit addresses: control,
 * the buffer's length, its address and the next descriptor's. The engine
 * owns a descriptor while OWN is set and clears it when done; FD and LD mark
 * the first and last, CH says the fourth word is a link, DIC keeps the
 * engine from interrupting on every one of them.
 *
 * Buffer 1's length field is thirteen bits, so a descriptor carries at most
 * 8191 bytes; 4096 per descriptor keeps every buffer inside a page, which is
 * what NetBSD and Linux both do.
 */
struct dw_idmac_desc {
	uint32_t	des0;
	uint32_t	des1;
	uint32_t	des2;
	uint32_t	des3;
};

#define DW_DES0_DIC		(1u << 1)
#define DW_DES0_LD		(1u << 2)
#define DW_DES0_FD		(1u << 3)
#define DW_DES0_CH		(1u << 4)
#define DW_DES0_ER		(1u << 5)
#define DW_DES0_CES		(1u << 30)
#define DW_DES0_OWN		(1u << 31)
#define DW_DESC_MAX_LEN		4096

#endif /* _SDMMC_DWMSHC_H */
