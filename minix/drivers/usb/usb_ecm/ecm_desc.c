/*
 * The bytes of a CDC Ethernet device: its configuration, its strings, its
 * notifications.  The rules are Linux's, because the devices were made for
 * Linux's host side - usbnet_generic_cdc_bind() and usbnet_get_endpoints()
 * (drivers/net/usb/usbnet.c, cdc_ether.c) - and each refusal below is one
 * of theirs, named.
 *
 *   the control interface   the first interface of class 2 (communications),
 *                           subclass 6 (Ethernet networking), protocol 0,
 *                           in alternate 0
 *   its functional          CS_INTERFACE descriptors anywhere among the
 *   descriptors             interface's own: a header, a union, and the
 *                           Ethernet descriptor (iMACAddress, wMaxSegment-
 *                           Size); a device without all three is refused,
 *                           as Linux refuses it
 *   the union               names the data interface; a union with master
 *                           and subordinate the wrong way round (some cable
 *                           modems) is turned round, as Linux does
 *   notifications           the control interface's endpoint, when it has
 *                           exactly one, and then it has to be interrupt IN
 *   the data interface      class 10 in alternate 0; the first alternate
 *                           with a bulk IN and a bulk OUT carries the frames
 */

#include "ecm_desc.h"

#define DT_INTERFACE	4
#define DT_ENDPOINT	5
#define DT_CS_INTERFACE	0x24

#define CDC_HEADER	0x00
#define CDC_UNION	0x06
#define CDC_ETHERNET	0x0f

#define NOTE_HEADER	8		/* bmRequestType ... wLength */
#define NOTE_NETWORK_CONNECTION	0x00
#define NOTE_SPEED_CHANGE	0x2a

static unsigned
le16(const uint8_t *p)
{
	return p[0] | ((unsigned)p[1] << 8);
}

static uint32_t
le32(const uint8_t *p)
{
	return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
	    ((uint32_t)p[3] << 24);
}

int
ecm_parse_config(const uint8_t *cfg, size_t len, struct ecm_info *info)
{
	size_t off;
	unsigned iface = 0, alt = 0, cls = 0, neps = 0, master = 0, slave = 0;
	int in_ctrl = 0, found = 0, header = 0, have_union = 0, ether = 0;
	int data_class = -1, in_data = 0, pair = 0;
	unsigned in_ep = 0, in_size = 0, out_ep = 0, out_size = 0;
	const uint8_t *note = NULL;
	const uint8_t *d;

	*info = (struct ecm_info){ 0 };

	/* The control interface and what it says. */
	for (off = 0; off + 2 <= len && cfg[off] >= 2; off += cfg[off]) {
		d = cfg + off;
		if (d[1] == DT_INTERFACE && off + 9 <= len) {
			if (found && in_ctrl)
				break;		/* its descriptors end here */
			in_ctrl = !found && d[3] == 0 && d[5] == 2 &&
			    d[6] == 6 && d[7] == 0;
			if (in_ctrl) {
				found = 1;
				info->ctrl_iface = d[2];
			}
			continue;
		}
		if (!in_ctrl)
			continue;
		if (d[1] == DT_CS_INTERFACE && d[0] >= 3 && off + d[0] <= len) {
			if (d[2] == CDC_HEADER && d[0] >= 5)
				header = 1;
			else if (d[2] == CDC_UNION && d[0] >= 5 && !have_union) {
				have_union = 1;
				master = d[3];
				slave = d[4];
			} else if (d[2] == CDC_ETHERNET && d[0] >= 13 && !ether) {
				ether = 1;
				info->mac_string = d[3];
				info->max_segment = le16(d + 8);
			}
		} else if (d[1] == DT_ENDPOINT && off + 7 <= len) {
			if (neps++ == 0)
				note = d;
		}
	}
	if (!found)
		return ECM_NO_CONTROL;
	if (!header)
		return ECM_NO_HEADER;
	if (!have_union)
		return ECM_NO_UNION;
	if (!ether)
		return ECM_NO_ETHERNET;

	/* The union the right way round, or turned round. */
	if (master == info->ctrl_iface)
		info->data_iface = slave;
	else if (slave == info->ctrl_iface)
		info->data_iface = master;
	else
		return ECM_NO_UNION;
	if (info->data_iface == info->ctrl_iface)
		return ECM_NO_DATA;

	if (neps == 1) {
		if (!(note[2] & 0x80) || (note[3] & 0x3) != 3)
			return ECM_BAD_NOTE;
		info->note_ep = note[2] & 0x0f;
		info->note_size = le16(note + 4) & 0x7ff;
	}

	/*
	 * The data interface: its class in alternate 0, and the first
	 * alternate that has both halves of a bulk pair.
	 */
	for (off = 0; off + 2 <= len && cfg[off] >= 2; off += cfg[off]) {
		d = cfg + off;
		if (d[1] == DT_INTERFACE && off + 9 <= len) {
			if (in_data && in_ep != 0 && out_ep != 0) {
				pair = 1;
				break;
			}
			iface = d[2];
			alt = d[3];
			cls = d[5];
			in_data = iface == info->data_iface;
			if (in_data && alt == 0)
				data_class = (int)cls;
			in_ep = out_ep = 0;
			continue;
		}
		if (in_data && d[1] == DT_ENDPOINT && off + 7 <= len &&
		    (d[3] & 0x3) == 2) {
			if ((d[2] & 0x80) && in_ep == 0) {
				in_ep = d[2] & 0x0f;
				in_size = le16(d + 4) & 0x7ff;
			} else if (!(d[2] & 0x80) && out_ep == 0) {
				out_ep = d[2] & 0x0f;
				out_size = le16(d + 4) & 0x7ff;
			}
		}
	}
	if (!pair && in_data && in_ep != 0 && out_ep != 0)
		pair = 1;
	if (data_class != 10)
		return ECM_NO_DATA;
	if (!pair || in_size == 0 || out_size == 0)
		return ECM_NO_BULK;

	info->data_alt = alt;
	info->bulk_in = in_ep;
	info->bulk_in_size = in_size;
	info->bulk_out = out_ep;
	info->bulk_out_size = out_size;
	return ECM_OK;
}

const char *
ecm_parse_error(int reason)
{
	switch (reason) {
	case ECM_OK:		return "CDC Ethernet";
	case ECM_NO_CONTROL:	return "no 2/6/0 interface in alternate 0";
	case ECM_NO_UNION:	return "no union that names the control "
				    "interface";
	case ECM_NO_HEADER:	return "no CDC header descriptor";
	case ECM_NO_ETHERNET:	return "no Ethernet functional descriptor";
	case ECM_NO_DATA:	return "the union names no data interface "
				    "(class 10)";
	case ECM_NO_BULK:	return "no alternate of the data interface "
				    "has a bulk pair";
	case ECM_BAD_NOTE:	return "the control interface's one endpoint "
				    "is not interrupt in";
	default:		return "?";
	}
}

int
ecm_parse_langid(const uint8_t *s, size_t len, unsigned *langid)
{
	if (len < 4 || s[0] < 4 || s[1] != 3)
		return 0;
	*langid = le16(s + 2);
	return 1;
}

/*
 * iMACAddress: twelve hexadecimal digits, UTF-16, most significant first
 * (ECM 1.2, 5.4).  Exactly twelve, as Linux checks - usb_string() and then
 * "bad MAC string" unless its length is 12.
 */
int
ecm_parse_mac(const uint8_t *s, size_t len, uint8_t mac[6])
{
	size_t n = len < 2 ? 0 : (s[0] < len ? s[0] : len);
	unsigned i, v;
	uint8_t out[6] = { 0 };

	if (n < 2 || s[1] != 3 || (n - 2) / 2 != 12)
		return 0;
	for (i = 0; i < 12; i++) {
		unsigned c = s[2 + 2 * i];

		if (s[3 + 2 * i] != 0)
			return 0;
		if (c >= '0' && c <= '9')
			v = c - '0';
		else if (c >= 'a' && c <= 'f')
			v = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			v = c - 'A' + 10;
		else
			return 0;
		out[i / 2] |= (uint8_t)(v << ((i & 1) ? 0 : 4));
	}
	for (i = 0; i < 6; i++)
		mac[i] = out[i];
	return 1;
}

/*
 * One interrupt transfer's worth.  Answers 1 when it completes a
 * notification this driver acts on, and fills in *note.
 *
 * SPEED_CHANGE is a header of eight bytes and two rates of four; on an
 * endpoint of eight bytes those are two transfers, and Linux takes the
 * second whole as the rates when the first came alone (EVENT_STS_SPLIT).
 * So does this - and anything that is not exactly the header plus the
 * rates counts as the first half, as it does there.
 */
int
ecm_note_feed(struct ecm_notes *st, const uint8_t *buf, size_t len,
	struct ecm_note *note)
{
	*note = (struct ecm_note){ ECM_NOTE_NONE, 0, 0, 0 };

	if (len < NOTE_HEADER)
		return 0;
	if (st->split) {
		st->split = 0;
		note->what = ECM_NOTE_SPEED;
		note->down = le32(buf);
		note->up = le32(buf + 4);
		return 1;
	}

	switch (buf[1]) {
	case NOTE_NETWORK_CONNECTION:
		note->what = ECM_NOTE_CONNECTION;
		note->connected = le16(buf + 2);
		return 1;
	case NOTE_SPEED_CHANGE:
		if (len != NOTE_HEADER + 8) {
			st->split = 1;
			return 0;
		}
		note->what = ECM_NOTE_SPEED;
		note->down = le32(buf + 8);
		note->up = le32(buf + 12);
		return 1;
	default:
		return 0;
	}
}

/*
 * A frame whose length is a multiple of the endpoint's packet ends in a
 * full packet, and the far side cannot tell it from the middle of a longer
 * one: it waits for more.  CDC Ethernet allows either a zero-length packet
 * or one byte more; this is the byte, which is what Linux sends for CDC
 * Ethernet (usbnet_start_xmit() without FLAG_SEND_ZLP) - a receiver takes
 * the frame by its own headers, and the byte is padding to it.
 */
size_t
ecm_tx_length(size_t len, unsigned max_packet)
{
	if (max_packet != 0 && len % max_packet == 0)
		return len + 1;
	return len;
}
