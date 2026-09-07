/*
 * SD/MMC on AArch64: what the layers are and where the line between them runs.
 *
 * The driver beside this header is a third program in a directory that
 * already had two, and the reason is the headers: sdhcreg.h and sdmmcreg.h
 * are the SD specification written down, and every MMC driver wants them.
 * What it does not share with mmc(8) and emmc(8) is code. Those two were
 * written for one board each - mmchost_mmchs.c asks minix/board.h which
 * BeagleBone it is running on - and their host interface (mmchost.h) puts
 * the card protocol *below* the host driver: card_initialize() is a host
 * method, so a second controller means writing CMD2, CMD3 and CMD9 a second
 * time. That is the wrong place for the line, and it is not the line this
 * port draws anywhere else: tty has one rs232.c above pl011.c and ns8250.c,
 * the kernel has one gic.c above gicv2.c and gicv3.c, and readclock has one
 * table of "compatible string -> probe". So:
 *
 *	sdmmc.c		the block device: minors, partitions, grants, ioctls
 *	sdmmc_card.c	the card: CMD0..CMD25, capacity, bus width, flush
 *	sdmmc_host.c	which controller this machine has, asked of the tree
 *	sdmmc_sdhci.c	one controller: the SD Host Controller register set
 *
 * A second controller is a second file beside sdmmc_sdhci.c and a second
 * line in the table, and nothing else moves. That matters here and not in
 * theory: the target board has two, and they are not the same part. The
 * eMMC behind mmc@fe310000 is "rockchip,rk3568-dwcmshc", which is the
 * standard SDHCI register set; the SD socket behind mmc@fe2b0000 is
 * "rockchip,rk3568-dw-mshc", a Synopsys design with its own registers and
 * its own way of changing the clock. This driver does the first.
 *
 * No board tables: the device is found by walking the device tree, and RS
 * grants its registers and interrupt from the same tree by way of a
 * devicetree "..." line in system.conf. That is the pattern stage 7.2 set
 * for every driver on this port.
 */

#ifndef _SDMMC_H
#define _SDMMC_H

#include <minix/type.h>
#include <minix/fdt.h>

#include <stdint.h>

/*
 * A response's shape, which is what the host controller has to be told: how
 * many bits come back, whether to check their CRC and index, and whether the
 * card holds DAT0 low afterwards. R4 and R5 are SDIO and not used here.
 */
#define SDMMC_RSP_NONE	0
#define SDMMC_RSP_R1	1	/* 48 bits, CRC and index checked */
#define SDMMC_RSP_R1B	2	/* R1, and the card is busy afterwards */
#define SDMMC_RSP_R2	3	/* 136 bits, CRC checked, no index */
#define SDMMC_RSP_R3	4	/* 48 bits, neither checked (OCR) */
#define SDMMC_RSP_R6	5	/* 48 bits, like R1 (published RCA) */
#define SDMMC_RSP_R7	6	/* 48 bits, like R1 (interface condition) */

#define SDMMC_DATA_NONE	0
#define SDMMC_DATA_READ	1
#define SDMMC_DATA_WRITE 2

/* The sector size this driver speaks, and the only one it sets. */
#define SDMMC_SECTOR_SIZE 512

/*
 * One command, filled in by the card layer and executed by the host.
 *
 * data points at memory the driver owns, never at a user page: the transfer
 * is done by the CPU through the controller's FIFO, and what a grant names
 * is copied in or out around it. See sdmmc.c for why that is not only the
 * simple choice.
 */
struct sdmmc_cmd {
	uint8_t		index;		/* CMD number, 0..63 */
	uint8_t		rsp_type;	/* SDMMC_RSP_* */
	uint8_t		data_dir;	/* SDMMC_DATA_* */
	uint8_t		stop;		/* end the transfer with CMD12 */
	uint32_t	arg;
	uint32_t	resp[4];	/* filled in by the host */
	void	       *data;
	uint32_t	blocks;
	uint32_t	blocklen;
};

/*
 * What the tree said about the controller, read once by sdmmc_host.c so that
 * each host driver does not repeat the same four property lookups.
 */
struct sdmmc_devinfo {
	phys_bytes	base;		/* "reg", first pair */
	size_t		size;
	int		irq;		/* GIC line, -1 when the tree has none */
	unsigned	bus_width;	/* "bus-width", 1 when absent */
	uint32_t	max_freq;	/* "max-frequency", 0 when absent */
	int		non_removable;	/* "non-removable" present */
};

/*
 * The host controller, as the card layer sees it. Exactly one is active, so
 * the driver behind these pointers keeps its state in file statics - the
 * same shape as pl031.c in readclock.
 */
struct sdmmc_host {
	const char     *name;		/* the compatible string that matched */
	int		(*init)(void);
	void		(*exit)(void);
	/*
	 * Ask for a card clock of at most hz, and answer with what was
	 * actually set: the divider is coarse and the card layer wants to
	 * report the truth rather than the wish. hz == 0 stops the clock.
	 */
	int		(*set_clock)(uint32_t hz, uint32_t *actual);
	int		(*set_bus_width)(unsigned bits);
	int		(*set_timing)(int high_speed);
	int		(*command)(struct sdmmc_cmd *cmd);

	unsigned	max_bus_width;	/* what the board wired up */
	uint32_t	max_freq;	/* what the board says it can take */
};

/* The card, as the block layer sees it. */
struct sdmmc_card {
	int		present;
	int		is_sd;		/* an SD card; otherwise an eMMC */
	int		sector_addressed; /* arguments are sectors, not bytes */
	uint32_t	rca;
	uint32_t	cid[4];
	uint32_t	csd[4];
	uint64_t	sectors;	/* capacity in 512-byte sectors */
	unsigned	bus_width;
	uint32_t	clock;		/* what the host actually set */
	int		high_speed;
	int		cache_on;	/* eMMC cache is on: flush means CMD6 */
	char		name[8];	/* product name out of the CID */
};

/*
 * How resp[] is numbered, because it is not how the hardware hands it over.
 *
 * The CSD and the CID accessors in sdmmcreg.h index the response as the
 * specification numbers it: bit 0 of resp[0] is bit 0 of the register, so
 * C_SIZE really is at bits 73 to 62. An SD host controller does not lay its
 * four response registers out that way - it drops the CRC7 and its stop bit
 * and hands over bits 127 to 8, right-shifted by eight.
 *
 * So a host driver shifts an R2 response back up by a byte before storing
 * it here, and the low byte reads as zero, which is where the CRC would
 * have been. Doing it once in the host is what lets the card layer use the
 * tree's accessors unchanged; doing it the other way round would mean a
 * second set of macros for every field.
 */

/* sdmmc_host.c: find the controller in the device tree. */
int sdmmc_host_find(struct sdmmc_host *host);

/* sdmmc_sdhci.c: the SD Host Controller standard register set. */
int sdhci_probe(const struct fdt_node *node, const struct sdmmc_devinfo *info,
	struct sdmmc_host *host);

/* sdmmc_card.c: the card protocol, in terms of the host above. */
int sdmmc_card_init(struct sdmmc_host *host, struct sdmmc_card *card);
int sdmmc_card_read(uint64_t sector, uint32_t count, void *buf);
int sdmmc_card_write(uint64_t sector, uint32_t count, const void *buf);
int sdmmc_card_flush(void);

/* Logging, shared so that one -args log_level= reaches every file. */
extern struct log sdmmc_log;

#endif /* _SDMMC_H */
