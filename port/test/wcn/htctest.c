/*
 * Host stand for minix/drivers/net/wcn/wcn_htc.c: HTC as the WCN3990's
 * target speaks it (port/clover/wifi/htc-wmi.md, section 2) - READY, the
 * connects, the one WMI credit and its return in a trailer - and frames
 * made to be wrong.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wcn_htc.h"

static int fails, checks;

#define CHECK(c, ...) do {						\
	checks++;							\
	if (!(c)) {							\
		fails++;						\
		printf("FAIL %s:%d: ", __FILE__, __LINE__);		\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

static uint8_t f[HTC_RX_MAX + 16];

/* A frame from the target: header, body, optional trailer. */
static size_t
frame(unsigned eid, const uint8_t *body, size_t n, const uint8_t *tr,
	size_t tl)
{

	f[0] = eid;
	f[1] = tl ? HTC_RXF_TRAILER : 0;
	f[2] = (n + tl) & 0xff;
	f[3] = (n + tl) >> 8;
	f[4] = tl;
	f[5] = f[6] = f[7] = 0;
	if (n)
		memcpy(f + 8, body, n);
	if (tl)
		memcpy(f + 8 + n, tr, tl);
	return 8 + n + tl;
}

static int
rx(struct htc *h, size_t n, unsigned *eid, const uint8_t **m, size_t *ml)
{

	return htc_rx(h, f, n, eid, m, ml);
}

int
main(void)
{
	struct htc h;
	const uint8_t *m;
	size_t ml, n, k;
	unsigned eid, pipe;
	uint8_t out[64], b[64];
	int r;

	htc_init(&h);

	/* READY, extended: 1 credit of 1792 bytes, 3 endpoints, HTC 2.1. */
	{
		uint8_t rd[12] = { 1, 0, 1, 0, 0x00, 0x07, 3, 0, 1, 32, 0, 0 };

		n = frame(0, rd, sizeof(rd), NULL, 0);
		CHECK(rx(&h, n, &eid, &m, &ml) == 0 && h.ready &&
		    h.credit_size == 1792 && h.max_ep == 3 &&
		    h.htc_version == 1, "ready");
	}
	/* READY with credit size 0: malformed. */
	{
		struct htc z;
		uint8_t rd[8] = { 1, 0, 1, 0, 0, 0, 3, 0 };

		htc_init(&z);
		n = frame(0, rd, sizeof(rd), NULL, 0);
		CHECK(rx(&z, n, &eid, &m, &ml) == -1 && !z.ready,
		    "zero credit size taken");
	}

	/* CONNECT messages: WMI with one credit, HTT without flow. */
	n = htc_connect_msg(b, HTC_SVC_WMI_CONTROL);
	CHECK(n == 8 && memcmp(b, "\x02\x00\x00\x01\x00\x01\x00\x00", 8) == 0,
	    "connect wmi");
	n = htc_connect_msg(b, HTC_SVC_HTT_DATA);
	CHECK(n == 8 && memcmp(b, "\x02\x00\x00\x03\x08\x00\x00\x00", 8) == 0,
	    "connect htt");
	n = htc_setup_complete_msg(b);
	CHECK(n == 12 && b[0] == 5 && b[1] == 0 && b[2] == 0 && b[11] == 0,
	    "setup complete");

	/* Endpoint 0 frame: header bytes, pipe 0. */
	memcpy(out + HTC_HDR, b, 12);
	CHECK(htc_frame(&h, 0, out, 12, &pipe) == 0 && pipe == 0 &&
	    out[0] == 0 && out[1] == 0 && out[2] == 12 && out[3] == 0 &&
	    out[5] == 0, "ep0 frame");
	CHECK(htc_frame(&h, 0, out, 12, &pipe) == 0 && out[5] == 1,
	    "ep0 seq");

	/* CONNECT_RESP: WMI on 1, HTT on 2. */
	{
		uint8_t cr[8] = { 3, 0, 0x00, 0x01, 0, 1, 0x00, 0x08 };

		n = frame(0, cr, 8, NULL, 0);
		CHECK(rx(&h, n, &eid, &m, &ml) == 0 && h.conn_done &&
		    h.conn_status == 0 && htc_svc_ep(&h, HTC_SVC_WMI_CONTROL) ==
		    1 && h.ep[1].ul == 3 && h.ep[1].dl == 2 &&
		    h.ep[1].credits == 1, "connect wmi resp");
		cr[2] = 0x00; cr[3] = 0x03; cr[5] = 2;
		n = frame(0, cr, 8, NULL, 0);
		CHECK(rx(&h, n, &eid, &m, &ml) == 0 &&
		    htc_svc_ep(&h, HTC_SVC_HTT_DATA) == 2 && h.ep[2].ul == 4 &&
		    !h.ep[2].credit_flow, "connect htt resp");
		/* The same endpoint again: not usable. */
		cr[2] = 0x01; cr[3] = 0x01;
		n = frame(0, cr, 8, NULL, 0);
		CHECK(rx(&h, n, &eid, &m, &ml) == 0 && h.conn_status == 0xff &&
		    h.ep[2].svc == HTC_SVC_HTT_DATA, "endpoint taken twice");
		/* Endpoint 9: outside. */
		cr[5] = 9;
		n = frame(0, cr, 8, NULL, 0);
		CHECK(rx(&h, n, &eid, &m, &ml) == 0 && h.conn_status == 0xff,
		    "endpoint 9");
		/* Refused. */
		cr[4] = 1; cr[5] = 3;
		n = frame(0, cr, 8, NULL, 0);
		CHECK(rx(&h, n, &eid, &m, &ml) == 0 && h.conn_status == 1 &&
		    h.ep[3].svc == 0, "refused connect");
	}

	/* WMI: one credit - the second command waits. */
	CHECK(htc_frame(&h, 1, out, 100, &pipe) == 0 && pipe == 3 &&
	    out[1] == HTC_TXF_NEED_CREDIT, "wmi first");
	CHECK(htc_frame(&h, 1, out, 100, &pipe) == EAGAIN, "wmi second sent");
	/* HTT: no flow. */
	CHECK(htc_frame(&h, 2, out, 100, &pipe) == 0 && pipe == 4 &&
	    out[1] == 0, "htt");
	CHECK(htc_frame(&h, 5, out, 100, &pipe) == EINVAL, "unconnected ep");

	/* A WMI event with the credit back in its trailer. */
	{
		uint8_t ev[6] = { 1, 0x90, 0, 0, 'e', 'v' };
		uint8_t tr[8] = { HTC_TR_CREDITS, 4, 0, 0, 1, 1, 0, 0 };

		n = frame(1, ev, 6, tr, 8);
		r = rx(&h, n, &eid, &m, &ml);
		CHECK(r == 1 && eid == 1 && ml == 6 && m[4] == 'e',
		    "event with trailer: %d %u %zu", r, eid, ml);
		CHECK(h.ep[1].credits == 1, "credit back: %u", h.ep[1].credits);
		CHECK(htc_frame(&h, 1, out, 100, &pipe) == 0, "wmi after credit");
	}
	/* A command longer than a credit pays for never goes. */
	h.ep[1].credits = 1;
	CHECK(htc_frame(&h, 1, out, 1800, &pipe) == EAGAIN, "two-credit cmd");

	/* Malformed. */
	{
		uint8_t ev[4] = { 1, 2, 3, 4 };
		uint8_t tr[4] = { HTC_TR_CREDITS, 8, 0, 0 };	/* past end */
		uint8_t lb[4] = { HTC_TR_LOOKAHEAD_BUNDLE, 0, 0, 0 };

		n = frame(1, ev, 4, tr, 4);
		CHECK(rx(&h, n, &eid, &m, &ml) == -1, "trailer past end");
		n = frame(1, ev, 4, lb, 4);
		CHECK(rx(&h, n, &eid, &m, &ml) == -1, "empty bundle");
		n = frame(1, ev, 4, NULL, 0);
		f[4] = 9; f[1] = HTC_RXF_TRAILER;	/* trailer > len */
		CHECK(rx(&h, n, &eid, &m, &ml) == -1, "trailer > len");
		n = frame(7, ev, 4, NULL, 0);
		CHECK(rx(&h, n, &eid, &m, &ml) == -1, "unconnected rx");
		n = frame(1, ev, 4, NULL, 0);
		CHECK(rx(&h, n - 1, &eid, &m, &ml) == -1, "short frame");
		f[2] = 0xff; f[3] = 0x0f;
		CHECK(rx(&h, sizeof(f), &eid, &m, &ml) == -1, "over 4096");
		/* A frame with only a trailer: nothing for anyone. */
		{
			uint8_t t2[4] = { HTC_TR_LOOKAHEAD, 0, 0, 0 };
			n = frame(1, NULL, 0, t2, 4);
			CHECK(rx(&h, n, &eid, &m, &ml) == 0, "trailer only");
		}
		/* Every truncation of a good frame: no read past it. */
		{
			uint8_t ev2[6] = { 1, 0x90, 0, 0, 'e', 'v' };
			uint8_t tr2[8] = { HTC_TR_CREDITS, 4, 0, 0, 1, 1, 0, 0 };
			n = frame(1, ev2, 6, tr2, 8);
			for (k = 0; k < n; k++) {
				uint8_t *c = malloc(k ? k : 1);
				memcpy(c, f, k);
				(void)htc_rx(&h, c, k, &eid, &m, &ml);
				free(c);
			}
		}
	}
	CHECK(h.dropped >= 6, "dropped %lu", h.dropped);

	/* Pipes: receive-only services do not connect. */
	{
		uint8_t ul, dl;
		CHECK(htc_svc_pipes(0x0301, &ul, &dl) == -1, "data2 pipes");
		CHECK(htc_svc_pipes(0x0104, &ul, &dl) == 0 && ul == 3 &&
		    dl == 2, "wmi vo pipes");
	}

	printf("htc: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
