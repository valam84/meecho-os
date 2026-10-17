#ifndef _USB_ECM_DESC_H
#define _USB_ECM_DESC_H

/*
 * What a CDC Ethernet device says about itself, read out of the bytes it
 * answers with.  Nothing of MINIX in here: the host stand builds this file
 * as it is (port/test/usb_ecm/).
 */

#include <stddef.h>
#include <stdint.h>

/*
 * The parts of a configuration this driver needs (CDC 1.2, 5.2.3; ECM
 * 1.2, 5.4).  Endpoints are numbers without the direction bit; sizes are
 * wMaxPacketSize's packet size.
 */
struct ecm_info {
	unsigned ctrl_iface;		/* communications, 2/6/0 */
	unsigned data_iface;		/* data, class 10: the union's first */
	unsigned data_alt;		/* its alternate with the bulk pair */
	unsigned bulk_in, bulk_in_size;
	unsigned bulk_out, bulk_out_size;
	unsigned note_ep, note_size;	/* the notifications; 0: none */
	unsigned mac_string;		/* iMACAddress */
	unsigned max_segment;		/* wMaxSegmentSize */
};

/* What ecm_parse_config() answers: ECM_OK, or why not. */
#define ECM_OK			0
#define ECM_NO_CONTROL		1
#define ECM_NO_UNION		2
#define ECM_NO_ETHERNET		3
#define ECM_NO_DATA		4
#define ECM_NO_BULK		5
#define ECM_BAD_NOTE		6
#define ECM_NO_HEADER		7

int ecm_parse_config(const uint8_t *cfg, size_t len, struct ecm_info *info);
const char *ecm_parse_error(int reason);

/* String descriptors: 1 when read, 0 when not. */
int ecm_parse_langid(const uint8_t *s, size_t len, unsigned *langid);
int ecm_parse_mac(const uint8_t *s, size_t len, uint8_t mac[6]);

/*
 * Notifications (CDC 1.2, 6.3).  An interrupt transfer carries one packet
 * of them, and SPEED_CHANGE (16 bytes) arrives in two on an endpoint of 8
 * - so the parser keeps a little state, as Linux does (EVENT_STS_SPLIT in
 * drivers/net/usb/cdc_ether.c).
 */
#define ECM_NOTE_NONE		0
#define ECM_NOTE_CONNECTION	1
#define ECM_NOTE_SPEED		2

struct ecm_notes {
	int split;			/* a SPEED_CHANGE waits for its rates */
};

struct ecm_note {
	int what;
	unsigned connected;		/* NETWORK_CONNECTION's wValue */
	uint32_t down, up;		/* SPEED_CHANGE, bits per second */
};

int ecm_note_feed(struct ecm_notes *st, const uint8_t *buf, size_t len,
	struct ecm_note *note);

/* SET_ETHERNET_PACKET_FILTER (ECM 1.2, 6.2.4). */
#define ECM_FILTER_PROMISCUOUS	0x01
#define ECM_FILTER_ALL_MULTICAST 0x02
#define ECM_FILTER_DIRECTED	0x04
#define ECM_FILTER_BROADCAST	0x08
#define ECM_FILTER_MULTICAST	0x10

/* How many bytes a frame of len takes on a bulk OUT endpoint. */
size_t ecm_tx_length(size_t len, unsigned max_packet);

#endif /* _USB_ECM_DESC_H */
