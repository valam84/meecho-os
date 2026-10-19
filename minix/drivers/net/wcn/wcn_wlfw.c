/*
 * wlfw messages (wcn_wlfw.h).  QMI as Linux's qmi_encdec puts it on the
 * wire: a 7-byte header, then TLVs - u8 type, le16 length, the value; an
 * optional field absent is a TLV absent; a counted array's count is one
 * byte where the C field counting it is one byte wide, two otherwise.
 */

#include <errno.h>
#include <string.h>

#include "wcn_wlfw.h"

/* ---- writing ---- */

static void
put(struct qmi_buf *b, const void *v, size_t n)
{

	if (b->err || b->cap - b->len < n) {
		b->err = 1;
		return;
	}
	memcpy(b->p + b->len, v, n);
	b->len += n;
}

static void
put8(struct qmi_buf *b, uint8_t v)
{

	put(b, &v, 1);
}

static void
put16(struct qmi_buf *b, uint16_t v)
{
	uint8_t x[2] = { v, v >> 8 };

	put(b, x, 2);
}

static void
put32(struct qmi_buf *b, uint32_t v)
{
	uint8_t x[4] = { v, v >> 8, v >> 16, v >> 24 };

	put(b, x, 4);
}

static void
put64(struct qmi_buf *b, uint64_t v)
{

	put32(b, (uint32_t)v);
	put32(b, (uint32_t)(v >> 32));
}

static void
begin(struct qmi_buf *b, uint16_t txn, uint16_t id)
{

	b->len = 0;
	b->err = 0;
	put8(b, QMI_REQUEST);
	put16(b, txn);
	put16(b, id);
	put16(b, 0);		/* msg_len, set by end() */
}

static size_t
end(struct qmi_buf *b)
{
	size_t n;

	if (b->err || b->len - QMI_HDR > 0xffff)
		return 0;
	n = b->len - QMI_HDR;
	b->p[5] = n;
	b->p[6] = n >> 8;
	return b->len;
}

/* A TLV whose value is written next: its header, length patched later. */
static size_t
tlv_open(struct qmi_buf *b, uint8_t type)
{

	put8(b, type);
	put16(b, 0);
	return b->len;
}

static void
tlv_close(struct qmi_buf *b, size_t at)
{
	size_t n;

	if (b->err)
		return;
	n = b->len - at;
	b->p[at - 2] = n;
	b->p[at - 1] = n >> 8;
}

static void
tlv8(struct qmi_buf *b, uint8_t type, uint8_t v)
{

	put8(b, type);
	put16(b, 1);
	put8(b, v);
}

static void
tlv32(struct qmi_buf *b, uint8_t type, uint32_t v)
{

	put8(b, type);
	put16(b, 4);
	put32(b, v);
}

size_t
wlfw_ind_register(struct qmi_buf *b, uint16_t txn)
{

	begin(b, txn, WLFW_IND_REGISTER);
	tlv8(b, 0x10, 1);		/* fw_ready_enable */
	tlv8(b, 0x13, 1);		/* msa_ready_enable */
	tlv32(b, 0x15, WLFW_CLIENT_ID);	/* client_id */
	return end(b);
}

size_t
wlfw_host_cap(struct qmi_buf *b, uint16_t txn)
{

	/*
	 * qcom,snoc-host-cap-8bit-quirk: the firmware of SDM630/660 reads
	 * daemon_support as one byte, not the four of the full message.
	 */
	begin(b, txn, WLFW_HOST_CAP);
	tlv8(b, 0x10, 0);		/* daemon_support: no */
	return end(b);
}

size_t
wlfw_msa_info(struct qmi_buf *b, uint16_t txn, uint64_t addr, uint32_t size)
{

	begin(b, txn, WLFW_MSA_INFO);
	put8(b, 0x01);
	put16(b, 8);
	put64(b, addr);
	tlv32(b, 0x02, size);
	return end(b);
}

size_t
wlfw_msa_ready(struct qmi_buf *b, uint16_t txn)
{

	begin(b, txn, WLFW_MSA_READY);
	return end(b);
}

size_t
wlfw_cap(struct qmi_buf *b, uint16_t txn)
{

	begin(b, txn, WLFW_CAP);
	return end(b);
}

size_t
wlfw_bdf(struct qmi_buf *b, uint16_t txn, uint32_t total, uint32_t seg,
	const uint8_t *data, size_t n, int end_)
{
	size_t at;

	if (n > WLFW_BDF_SEG)
		return 0;
	begin(b, txn, WLFW_BDF_DOWNLOAD);
	tlv8(b, 0x01, 1);		/* valid */
	tlv32(b, 0x10, 0);		/* file_id: the board data */
	tlv32(b, 0x11, total);
	tlv32(b, 0x12, seg);
	at = tlv_open(b, 0x13);		/* data: a 16-bit count */
	put16(b, n);
	put(b, data, n);
	tlv_close(b, at);
	tlv8(b, 0x14, end_ != 0);	/* end: on every segment, as ath10k */
	return end(b);
}

size_t
wlfw_cal_report(struct qmi_buf *b, uint16_t txn)
{

	begin(b, txn, WLFW_CAL_REPORT);
	tlv8(b, 0x01, 0);		/* meta_data: none (count 0, one byte) */
	return end(b);
}

/*
 * The tables of WLAN_CFG - Linux's target_ce_config_wlan,
 * target_service_to_ce_map_wlan (with its zero entry, which goes on the
 * wire too) and target_shadow_reg_cfg_map.  CE7's direction 4 is no
 * PIPEDIR value: as Linux sends it.
 */
static const uint32_t tgt_cfg[WLFW_NCE][5] = {
	/* pipe, dir, nentries, nbytes_max, flags */
	{ 0, 2, 32, 2048, 0 },
	{ 1, 1, 32, 2048, 0 },
	{ 2, 1, 64, 2048, 0 },
	{ 3, 2, 32, 2048, 0 },
	{ 4, 2, 256, 256, 8 },
	{ 5, 2, 1024, 64, 8 },
	{ 6, 3, 32, 16384, 0 },
	{ 7, 4, 0, 0, 8 },
	{ 8, 1, 32, 2048, 0 },
	{ 9, 1, 32, 2048, 0 },
	{ 10, 1, 32, 2048, 0 },
	{ 11, 1, 32, 2048, 0 },
};

static const uint32_t svc_cfg[WLFW_NSVC][3] = {
	/* service, dir, pipe */
	{ 0x0104, 2, 3 }, { 0x0104, 1, 2 },	/* WMI data VO */
	{ 0x0102, 2, 3 }, { 0x0102, 1, 2 },	/* BK */
	{ 0x0101, 2, 3 }, { 0x0101, 1, 2 },	/* BE */
	{ 0x0103, 2, 3 }, { 0x0103, 1, 2 },	/* VI */
	{ 0x0100, 2, 3 }, { 0x0100, 1, 2 },	/* WMI control */
	{ 0x0001, 2, 0 }, { 0x0001, 1, 2 },	/* HTC control */
	{ 0xfe00, 2, 0 }, { 0xfe00, 1, 2 },	/* raw test streams */
	{ 0x0300, 2, 4 }, { 0x0300, 1, 1 },	/* HTT data */
	{ 0xfe00, 2, 5 },			/* raw test streams */
	{ 0x0301, 1, 9 },			/* HTT data 2 */
	{ 0x0302, 1, 10 },			/* HTT data 3 */
	{ 0x0600, 1, 11 },			/* HTT log */
	{ 0, 0, 0 },
};

static const uint16_t shadow[WLFW_NSHADOW][2] = {
	/* CE, register: 0x3c a send ring's write index, 0x40 a receive's */
	{ 0, 0x3c }, { 3, 0x3c }, { 4, 0x3c }, { 5, 0x3c }, { 7, 0x3c },
	{ 1, 0x40 }, { 2, 0x40 }, { 7, 0x40 }, { 8, 0x40 }, { 9, 0x40 },
	{ 10, 0x40 }, { 11, 0x40 },
};

size_t
wlfw_wlan_cfg(struct qmi_buf *b, uint16_t txn)
{
	size_t at;
	unsigned i, k;

	begin(b, txn, WLFW_WLAN_CFG);
	at = tlv_open(b, 0x11);		/* tgt_cfg: one-byte count */
	put8(b, WLFW_NCE);
	for (i = 0; i < WLFW_NCE; i++)
		for (k = 0; k < 5; k++)
			put32(b, tgt_cfg[i][k]);
	tlv_close(b, at);
	at = tlv_open(b, 0x12);		/* svc_cfg */
	put8(b, WLFW_NSVC);
	for (i = 0; i < WLFW_NSVC; i++)
		for (k = 0; k < 3; k++)
			put32(b, svc_cfg[i][k]);
	tlv_close(b, at);
	at = tlv_open(b, 0x13);		/* shadow_reg */
	put8(b, WLFW_NSHADOW);
	for (i = 0; i < WLFW_NSHADOW; i++) {
		put16(b, shadow[i][0]);
		put16(b, shadow[i][1]);
	}
	tlv_close(b, at);
	return end(b);
}

size_t
wlfw_wlan_mode(struct qmi_buf *b, uint16_t txn, uint32_t mode)
{

	begin(b, txn, WLFW_WLAN_MODE);
	tlv32(b, 0x01, mode);
	tlv8(b, 0x10, 0);		/* hw_debug: 0, always given */
	return end(b);
}

size_t
wlfw_ini(struct qmi_buf *b, uint16_t txn, uint8_t fwlog)
{

	begin(b, txn, WLFW_INI);
	tlv8(b, 0x10, fwlog);
	return end(b);
}

/* ---- reading ---- */

static uint16_t
get16(const uint8_t *p)
{

	return p[0] | p[1] << 8;
}

static uint32_t
get32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static uint64_t
get64(const uint8_t *p)
{

	return get32(p) | (uint64_t)get32(p + 4) << 32;
}

int
qmi_parse(const uint8_t *buf, size_t len, struct qmi_msg *m)
{

	if (len < QMI_HDR || get16(buf + 5) != len - QMI_HDR)
		return EINVAL;
	m->type = buf[0];
	m->txn = get16(buf + 1);
	m->id = get16(buf + 3);
	m->body = buf + QMI_HDR;
	m->len = len - QMI_HDR;
	if (m->type != QMI_REQUEST && m->type != QMI_RESPONSE &&
	    m->type != QMI_INDICATION)
		return EINVAL;
	return 0;
}

int
qmi_tlv(const struct qmi_msg *m, unsigned type, const uint8_t **v,
	size_t *vlen)
{
	size_t at = 0, l;
	int found = 0;

	while (at < m->len) {
		if (m->len - at < 3)
			return -1;
		l = get16(m->body + at + 1);
		if (l > m->len - at - 3)
			return -1;
		if (m->body[at] == type && !found) {
			*v = m->body + at + 3;
			*vlen = l;
			found = 1;
		}
		at += 3 + l;
	}
	return found;
}

/*
 * Below 0x10 a TLV is mandatory: one the message is not known to carry is
 * a message we do not understand, and Linux refuses it whole too.  "known"
 * has bit t set for each such type t the message has.
 */
static int
known_only(const struct qmi_msg *m, unsigned known)
{
	size_t at = 0, l;

	while (at < m->len) {
		if (m->len - at < 3)
			return 0;
		l = get16(m->body + at + 1);
		if (l > m->len - at - 3)
			return 0;
		if (m->body[at] < 0x10 && !(known & (1u << m->body[at])))
			return 0;
		at += 3 + l;
	}
	return 1;
}

int
qmi_result(const struct qmi_msg *m, unsigned *result, unsigned *error)
{
	const uint8_t *v;
	size_t l;

	if (!known_only(m, ~0u) || qmi_tlv(m, 0x02, &v, &l) != 1 || l != 4)
		return EINVAL;
	*result = get16(v);
	*error = get16(v + 2);
	return 0;
}

int
wlfw_ind_register_resp(const struct qmi_msg *m, uint64_t *fw_status)
{
	const uint8_t *v;
	size_t l;
	int r;

	*fw_status = 0;
	if (!known_only(m, 1u << 0x02) || (r = qmi_tlv(m, 0x10, &v, &l)) < 0)
		return EINVAL;
	if (r == 1) {
		if (l != 8)
			return EINVAL;
		*fw_status = get64(v);
	}
	return 0;
}

int
wlfw_msa_info_resp(const struct qmi_msg *m, uint64_t msa, uint32_t msa_size,
	struct wlfw_region *r, unsigned *n)
{
	const uint8_t *v;
	size_t l;
	unsigned i, cnt;

	*n = 0;
	if (!known_only(m, 1u << 0x02 | 1u << 0x03) ||
	    qmi_tlv(m, 0x03, &v, &l) != 1 || l < 1)
		return EINVAL;
	cnt = v[0];
	if (cnt > WLFW_MSA_REGIONS || l != 1 + 13 * (size_t)cnt)
		return EINVAL;
	for (i = 0; i < cnt; i++) {
		const uint8_t *e = v + 1 + 13 * i;

		r[i].addr = get64(e);
		r[i].size = get32(e + 8);
		r[i].secure = e[12];
		if (r[i].addr < msa || r[i].size > msa_size ||
		    r[i].addr - msa > msa_size - r[i].size)
			return EINVAL;
	}
	*n = cnt;
	return 0;
}

int
wlfw_cap_resp(const struct qmi_msg *m, struct wlfw_cap *c)
{
	const uint8_t *v;
	size_t l;
	int r;

	memset(c, 0, sizeof(*c));
	c->chip_id = 0xff;
	c->board_id = 0xff;

	if (!known_only(m, 1u << 0x02))
		return EINVAL;
	if ((r = qmi_tlv(m, 0x10, &v, &l)) < 0 || (r == 1 && l != 8))
		return EINVAL;
	if (r == 1) {
		c->has_chip = 1;
		c->chip_id = get32(v);
		c->chip_family = get32(v + 4);
	}
	if ((r = qmi_tlv(m, 0x11, &v, &l)) < 0 || (r == 1 && l != 4))
		return EINVAL;
	if (r == 1) {
		c->has_board = 1;
		c->board_id = get32(v);
	}
	if ((r = qmi_tlv(m, 0x12, &v, &l)) < 0 || (r == 1 && l != 4))
		return EINVAL;
	if (r == 1) {
		c->has_soc = 1;
		c->soc_id = get32(v);
	}
	/* fw_version, then a string with a one-byte length, under 33. */
	if ((r = qmi_tlv(m, 0x13, &v, &l)) < 0)
		return EINVAL;
	if (r == 1) {
		if (l < 5 || v[4] >= sizeof(c->fw_timestamp) ||
		    l != 5 + (size_t)v[4])
			return EINVAL;
		c->has_fw = 1;
		c->fw_version = get32(v);
		memcpy(c->fw_timestamp, v + 5, v[4]);
		c->fw_timestamp[v[4]] = '\0';
	}
	/* The build id: the TLV is the string, no length byte, under 129. */
	if ((r = qmi_tlv(m, 0x14, &v, &l)) < 0)
		return EINVAL;
	if (r == 1) {
		if (l >= sizeof(c->fw_build_id))
			return EINVAL;
		memcpy(c->fw_build_id, v, l);
		c->fw_build_id[l] = '\0';
	}
	return 0;
}
