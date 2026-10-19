#ifndef _WCN_FW_H
#define _WCN_FW_H

/*
 * The two files the host side of the WCN3990 reads (port/clover/hw-wifi.md,
 * "Файлы прошивки"): firmware-N.bin, which on this chip carries no code at
 * all - the WLAN firmware runs inside the modem - only what the host must
 * know about it; and board-2.bin, the board data files of many boards, of
 * which one is chosen by name and sent to the firmware over QMI.
 *
 * Both are a magic string, NUL-terminated and padded to 4, then a list of
 * elements: a 32-bit id, a 32-bit length, the data, padded to 4; all
 * little-endian.  board-2.bin nests: an element of the outer list holds a
 * list of (name, data) elements.
 *
 * No MINIX here: the host stand (port/test/wcn) builds this file with the
 * host's compiler and checks it against the files the tablet uses.
 */

#include <stddef.h>
#include <stdint.h>

/* firmware-N.bin elements. */
#define WCN_FW_IE_FW_VERSION		0
#define WCN_FW_IE_TIMESTAMP		1
#define WCN_FW_IE_FEATURES		2
#define WCN_FW_IE_FW_IMAGE		3
#define WCN_FW_IE_OTP_IMAGE		4
#define WCN_FW_IE_WMI_OP_VERSION	5
#define WCN_FW_IE_HTT_OP_VERSION	6
#define WCN_FW_IE_CODE_SWAP_IMAGE	7

/* Feature bits of WCN_FW_IE_FEATURES - the ones this firmware has. */
#define WCN_FW_FEAT_WOWLAN		6
#define WCN_FW_FEAT_NO_4ADDR_PAD	8	/* native-wifi decap: no pad */
#define WCN_FW_FEAT_MESH_BCAST		16
#define WCN_FW_FEAT_MGMT_TX_BY_REF	18	/* mgmt frames by address */
#define WCN_FW_FEAT_NON_BMI		19	/* no BMI: QMI brings it up */
#define WCN_FW_FEAT_SINGLE_CHAN_INFO	20
#define WCN_FW_FEAT_MAX			64

#define WCN_FW_VERSION_MAX		64

struct wcn_fw_info {
	int api;			/* N of firmware-N.bin, the caller's */
	char version[WCN_FW_VERSION_MAX];	/* "" when the file has none */
	uint32_t timestamp;
	uint64_t features;		/* bit n: feature n */
	int wmi_op;			/* -1: not given */
	int htt_op;			/* -1: not given */
	size_t image_len;		/* FW_IMAGE, if any: none on WCN3990 */
};

int wcn_fw_parse(const uint8_t *file, size_t len, struct wcn_fw_info *fi);

/*
 * What firmware-5.bin of the tablet's Linux says, for when the file is
 * not there: MEECHO's own kit carries no firmware, and the tablet's
 * partitions have no such file - only the modem's image with the board
 * data.  The values belong to WLAN.HL.1.0.1 (wmi-op 4, htt-op 3, the six
 * features); a firmware-N.bin found on the root wins over them.
 */
void wcn_fw_builtin(struct wcn_fw_info *fi);

/* board-2.bin: outer elements, inner elements of a BOARD element. */
#define WCN_BD_IE_BOARD			0
#define WCN_BD_IE_BOARD_EXT		1
#define WCN_BD_IE_NAME			0
#define WCN_BD_IE_DATA			1

#define WCN_BD_NAME_MAX			100

/*
 * The names a board is looked up by, most specific first: with the
 * calibration variant of the device tree (if any) and the chip id, with the
 * chip id alone, with the board id alone.  "which" is 0, 1 or 2; the
 * answer is the length, or -1 where that name is the same as the one
 * before it (no variant: 0 and 1 are one name).
 */
int wcn_bd_name(char *buf, size_t size, int which, unsigned board_id,
	unsigned chip_id, const char *variant);

/*
 * Find the board data for "name" in board-2.bin: 0 and [*data, *data +
 * *dlen) inside "file"; ENOENT when no board has that name; EINVAL when the
 * file is not well formed (a length that runs past its end, a wrong
 * magic) - checked before anything is read through it.
 */
int wcn_bd_find(const uint8_t *file, size_t len, const char *name,
	const uint8_t **data, size_t *dlen);

/*
 * The board data as the modem's image carries it: one raw file per board
 * id beside the firmware - bdwlan.bin for 0xff, bdwlan.bXX below 0x100,
 * bdwlan.XXX above (the downstream driver's rule; the tablet's
 * modem/image has bdwlan.bin, bdwlan.b04 ... bdwlan.b37, bdwlan.102 ...
 * bdwlan.109).  board-2.bin's "bus=snoc,qmi-board-id=ff" is byte for
 * byte that bdwlan.bin.  The order a driver looks: this file in the
 * modem's firmware directory first, board-2.bin after.  -1 if it does
 * not fit.
 */
int wcn_bdf_name(char *buf, size_t size, unsigned board_id);

/* Every name in board-2.bin, for a report: fn(name, namelen, datalen). */
int wcn_bd_walk(const uint8_t *file, size_t len,
	void (*fn)(const char *, size_t, size_t, void *), void *arg);

#endif /* _WCN_FW_H */
