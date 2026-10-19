/*
 * firmware-N.bin and board-2.bin of the WCN3990 (wcn_fw.h).  Every length
 * is checked against what is left of the file before anything is read
 * through it; the files come from the root file system, not from us.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "wcn_fw.h"

#define FW_MAGIC	"QCA-ATH10K"
#define BD_MAGIC	"QCA-ATH10K-BOARD"

#define ALIGN4(n)	(((n) + 3) & ~(size_t)3)

static uint32_t
le32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

/*
 * One element at *pos of [0, len): its id, data and length, and *pos past
 * it and its padding.  1: an element; 0: the end; -1: malformed.  The
 * padding of the last element may be missing - the length check is on the
 * data, as the reader of these files in Linux has it, so a file that works
 * there works here.
 */
static int
next_ie(const uint8_t *buf, size_t len, size_t *pos, uint32_t *id,
	const uint8_t **data, size_t *dlen)
{
	size_t left, l;

	if (*pos > len || len - *pos <= 8)
		return 0;
	left = len - *pos - 8;
	*id = le32(buf + *pos);
	l = le32(buf + *pos + 4);
	if (l > left)
		return -1;
	*data = buf + *pos + 8;
	*dlen = l;
	*pos += 8 + (ALIGN4(l) <= left ? ALIGN4(l) : left);
	return 1;
}

/* The magic, NUL-terminated, padded to 4: where the elements begin, or 0. */
static size_t
skip_magic(const uint8_t *file, size_t len, const char *magic)
{
	size_t m = strlen(magic) + 1;

	if (len < ALIGN4(m) || memcmp(file, magic, m) != 0)
		return 0;
	return ALIGN4(m);
}

int
wcn_fw_parse(const uint8_t *file, size_t len, struct wcn_fw_info *fi)
{
	const uint8_t *d;
	size_t pos, dl, i;
	uint32_t id;
	int r;

	memset(fi, 0, sizeof(*fi));
	fi->wmi_op = fi->htt_op = -1;
	if ((pos = skip_magic(file, len, FW_MAGIC)) == 0)
		return EINVAL;
	while ((r = next_ie(file, len, &pos, &id, &d, &dl)) > 0) {
		switch (id) {
		case WCN_FW_IE_FW_VERSION:
			if (dl >= sizeof(fi->version))
				return EINVAL;
			memcpy(fi->version, d, dl);
			fi->version[dl] = '\0';
			break;
		case WCN_FW_IE_TIMESTAMP:
			if (dl != 4)
				return EINVAL;
			fi->timestamp = le32(d);
			break;
		case WCN_FW_IE_FEATURES:
			/* A bitmap, byte by byte, low bit first. */
			for (i = 0; i < dl * 8 && i < WCN_FW_FEAT_MAX; i++)
				if (d[i / 8] & (1 << (i % 8)))
					fi->features |= (uint64_t)1 << i;
			break;
		case WCN_FW_IE_FW_IMAGE:
			fi->image_len = dl;
			break;
		case WCN_FW_IE_WMI_OP_VERSION:
			if (dl != 4)
				return EINVAL;
			fi->wmi_op = (int)le32(d);
			break;
		case WCN_FW_IE_HTT_OP_VERSION:
			if (dl != 4)
				return EINVAL;
			fi->htt_op = (int)le32(d);
			break;
		default:
			/* OTP and code swap images, unknown ids: not ours. */
			break;
		}
	}
	return r < 0 ? EINVAL : 0;
}

void
wcn_fw_builtin(struct wcn_fw_info *fi)
{

	memset(fi, 0, sizeof(*fi));
	fi->api = 5;
	fi->features = (uint64_t)1 << WCN_FW_FEAT_WOWLAN |
	    (uint64_t)1 << WCN_FW_FEAT_NO_4ADDR_PAD |
	    (uint64_t)1 << WCN_FW_FEAT_MESH_BCAST |
	    (uint64_t)1 << WCN_FW_FEAT_MGMT_TX_BY_REF |
	    (uint64_t)1 << WCN_FW_FEAT_NON_BMI |
	    (uint64_t)1 << WCN_FW_FEAT_SINGLE_CHAN_INFO;
	fi->wmi_op = 4;
	fi->htt_op = 3;
}

int
wcn_bdf_name(char *buf, size_t size, unsigned board_id)
{
	int n;

	if (board_id == 0xff)
		n = snprintf(buf, size, "bdwlan.bin");
	else if (board_id < 0x100)
		n = snprintf(buf, size, "bdwlan.b%02x", board_id);
	else
		n = snprintf(buf, size, "bdwlan.%03x", board_id);
	if (n < 0 || (size_t)n >= size)
		return -1;
	return n;
}

int
wcn_bd_name(char *buf, size_t size, int which, unsigned board_id,
	unsigned chip_id, const char *variant)
{
	int n;

	switch (which) {
	case 0:
		if (variant == NULL || variant[0] == '\0')
			return -1;	/* the same as 1 */
		n = snprintf(buf, size, "bus=snoc,qmi-board-id=%x,"
		    "qmi-chip-id=%x,variant=%s", board_id, chip_id, variant);
		break;
	case 1:
		n = snprintf(buf, size, "bus=snoc,qmi-board-id=%x,"
		    "qmi-chip-id=%x", board_id, chip_id);
		break;
	case 2:
		n = snprintf(buf, size, "bus=snoc,qmi-board-id=%x", board_id);
		break;
	default:
		return -1;
	}
	if (n < 0 || (size_t)n >= size)
		return -1;
	return n;
}

/*
 * One BOARD element: its (name, data) list.  A board may carry several
 * names; the data that follows any matching one is the answer.
 */
static int
board_ie(const uint8_t *b, size_t blen, const char *name,
	const uint8_t **data, size_t *dlen)
{
	const uint8_t *d;
	size_t pos = 0, dl;
	uint32_t id;
	int r, match = 0;

	while ((r = next_ie(b, blen, &pos, &id, &d, &dl)) > 0) {
		if (id == WCN_BD_IE_NAME) {
			if (dl == strlen(name) && memcmp(d, name, dl) == 0)
				match = 1;
		} else if (id == WCN_BD_IE_DATA && match) {
			*data = d;
			*dlen = dl;
			return 0;
		}
	}
	return r < 0 ? EINVAL : ENOENT;
}

int
wcn_bd_find(const uint8_t *file, size_t len, const char *name,
	const uint8_t **data, size_t *dlen)
{
	const uint8_t *d;
	size_t pos, dl;
	uint32_t id;
	int r, s;

	if ((pos = skip_magic(file, len, BD_MAGIC)) == 0)
		return EINVAL;
	while ((r = next_ie(file, len, &pos, &id, &d, &dl)) > 0) {
		/* BOARD_EXT is for a second board id over BMI: not here. */
		if (id != WCN_BD_IE_BOARD)
			continue;
		if ((s = board_ie(d, dl, name, data, dlen)) != ENOENT)
			return s;
	}
	return r < 0 ? EINVAL : ENOENT;
}

int
wcn_bd_walk(const uint8_t *file, size_t len,
	void (*fn)(const char *, size_t, size_t, void *), void *arg)
{
	const uint8_t *d, *bd, *nm;
	size_t pos, dl, bpos, bdl, nl;
	uint32_t id, bid;
	int r, s;

	if ((pos = skip_magic(file, len, BD_MAGIC)) == 0)
		return EINVAL;
	while ((r = next_ie(file, len, &pos, &id, &d, &dl)) > 0) {
		if (id != WCN_BD_IE_BOARD)
			continue;
		bpos = 0;
		nm = NULL;
		nl = 0;
		while ((s = next_ie(d, dl, &bpos, &bid, &bd, &bdl)) > 0) {
			if (bid == WCN_BD_IE_NAME) {
				nm = bd;
				nl = bdl;
			} else if (bid == WCN_BD_IE_DATA && nm != NULL)
				fn((const char *)nm, nl, bdl, arg);
		}
		if (s < 0)
			return EINVAL;
	}
	return r < 0 ? EINVAL : 0;
}
