#ifndef _WCN_WLFW_H
#define _WCN_WLFW_H

/*
 * The WLAN firmware's QMI service, wlfw (service 0x45, version 1), as the
 * WCN3990's host speaks it (port/clover/hw-wifi.md, section 3; byte
 * layouts in port/clover/wifi/wlfw.md).  Only the messages of the
 * bring-up, in the shapes this machine's tree asks for (the 8-bit
 * HOST_CAP, no xo calibration).
 *
 * No MINIX and no transport here: a message is built into a buffer and
 * an answer parsed out of one.  The daemon wcnd puts them on a QRTR
 * socket; the host stand (port/test/wcn) compares them with the bytes
 * Linux puts on the wire.
 */

#include <stddef.h>
#include <stdint.h>

#define WLFW_SERVICE		0x45
#define WLFW_VERSION		1
#define WLFW_INSTANCE		1	/* QRTR's word: version | instance << 8 */

/* QMI message types. */
#define QMI_REQUEST		0
#define QMI_RESPONSE		2
#define QMI_INDICATION		4
#define QMI_HDR			7

/* Message ids. */
#define WLFW_IND_REGISTER	0x0020
#define WLFW_FW_READY_IND	0x0021
#define WLFW_WLAN_MODE		0x0022
#define WLFW_WLAN_CFG		0x0023
#define WLFW_CAP		0x0024
#define WLFW_BDF_DOWNLOAD	0x0025
#define WLFW_CAL_REPORT		0x0026
#define WLFW_MSA_READY_IND	0x002b
#define WLFW_MSA_INFO		0x002d
#define WLFW_MSA_READY		0x002e
#define WLFW_INI		0x002f
#define WLFW_HOST_CAP		0x0034

#define WLFW_CLIENT_ID		0x4b4e454c	/* "LENK", as ath10k */
#define WLFW_FW_STATUS_READY	0x02		/* IND_REGISTER's fw_status */

#define WLFW_MODE_MISSION	0
#define WLFW_MODE_OFF		4

#define WLFW_BDF_SEG		6144	/* board data per BDF_DOWNLOAD */
#define WLFW_MSG_MAX		(QMI_HDR + 6182)

/* QMI's answer codes (the common TLV 0x02). */
#define QMI_ERR_MALFORMED	1
#define QMI_ERR_NOT_SUPPORTED	94

struct qmi_buf {
	uint8_t *p;
	size_t cap;
	size_t len;
	int err;			/* the buffer was too small */
};

/* Requests.  Each returns the message length, or 0 if it did not fit. */
size_t wlfw_ind_register(struct qmi_buf *b, uint16_t txn);
size_t wlfw_host_cap(struct qmi_buf *b, uint16_t txn);	/* 8-bit form */
size_t wlfw_msa_info(struct qmi_buf *b, uint16_t txn, uint64_t addr,
	uint32_t size);
size_t wlfw_msa_ready(struct qmi_buf *b, uint16_t txn);
size_t wlfw_cap(struct qmi_buf *b, uint16_t txn);
size_t wlfw_bdf(struct qmi_buf *b, uint16_t txn, uint32_t total,
	uint32_t seg, const uint8_t *data, size_t n, int end);
size_t wlfw_cal_report(struct qmi_buf *b, uint16_t txn);
size_t wlfw_wlan_cfg(struct qmi_buf *b, uint16_t txn);
size_t wlfw_wlan_mode(struct qmi_buf *b, uint16_t txn, uint32_t mode);
size_t wlfw_ini(struct qmi_buf *b, uint16_t txn, uint8_t fwlog);

/*
 * A message received: its header, checked against the datagram's length
 * (Linux reads the length off the datagram and ignores msg_len; here both
 * must agree).  0, or EINVAL.
 */
struct qmi_msg {
	unsigned type;
	uint16_t txn;
	uint16_t id;
	const uint8_t *body;
	size_t len;
};

int qmi_parse(const uint8_t *buf, size_t len, struct qmi_msg *m);

/*
 * The TLV "type" of a message: 1 and its value, 0 if absent, -1 if the
 * TLVs are malformed (a length past the end).  The parsers below also
 * refuse a mandatory TLV (type below 0x10) the message is not known to
 * carry, as Linux does.
 */
int qmi_tlv(const struct qmi_msg *m, unsigned type, const uint8_t **v,
	size_t *vlen);

/* The common answer: 0 and result/error, or EINVAL (missing, malformed). */
int qmi_result(const struct qmi_msg *m, unsigned *result, unsigned *error);

/* IND_REGISTER's answer: fw_status, 0 when not given. */
int wlfw_ind_register_resp(const struct qmi_msg *m, uint64_t *fw_status);

#define WLFW_MSA_REGIONS	2
struct wlfw_region {
	uint64_t addr;
	uint32_t size;
	uint8_t secure;
};

/*
 * MSA_INFO's answer: the regions, each checked to lie inside [msa,
 * msa + msa_size) - they go to TrustZone next, and a region outside is
 * memory not ours to give.
 */
int wlfw_msa_info_resp(const struct qmi_msg *m, uint64_t msa,
	uint32_t msa_size, struct wlfw_region *r, unsigned *n);

struct wlfw_cap {
	int has_chip, has_board, has_soc, has_fw;
	uint32_t chip_id, chip_family;
	uint32_t board_id;		/* 0xff when not given, as ath10k */
	uint32_t soc_id;
	uint32_t fw_version;
	char fw_timestamp[33];
	char fw_build_id[129];
};

int wlfw_cap_resp(const struct qmi_msg *m, struct wlfw_cap *c);

/* The tables WLAN_CFG carries (Linux's snoc.c), for reports and tests. */
#define WLFW_NCE		12
#define WLFW_NSVC		21	/* with the zero entry Linux sends */
#define WLFW_NSHADOW		12

#endif /* _WCN_WLFW_H */
