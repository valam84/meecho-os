/*
 * Host stand for qcom_rpm/glink.c: GLINK over the RPM message RAM and an
 * RPM request, against a model of the RPM.
 *
 * The model sits behind the kick (the APCS IPC bit): it reads what the
 * client put in ap2r, answers in r2ap as the RPM does - VERSION_ACK, its
 * own OPEN of "rpm_requests", OPEN_ACK, and for a request a "req" answer
 * with "msg#" and, for resources it does not have, "err" - and moves the
 * indices it owns.  Every word the client writes is checked against the
 * layout Linux writes (qcom_glink_native.c, smd-rpm.c), byte for byte.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glink.h"

static unsigned checks, failures;

#define CHECK(c, ...) do {						\
	checks++;							\
	if (!(c)) {							\
		failures++;						\
		printf("  FAIL: ");					\
		printf(__VA_ARGS__);					\
		printf("\n");						\
	}								\
} while (0)

#define RAM	0x7000
static uint8_t ram[RAM] __attribute__((aligned(8)));
static uint32_t TX, RX, LEN;		/* FIFO offsets (tail word) and length */

static uint32_t rd(uint32_t o) { uint32_t v; memcpy(&v, ram + o, 4); return v; }
static void wr(uint32_t o, uint32_t v) { memcpy(ram + o, &v, 4); }

/* The model. */
static struct {
	int remote_opens;	/* the RPM opens rpm_requests itself */
	int silent;		/* says nothing */
	int send_version;	/* answers VERSION with its own VERSION */
	unsigned rcid;
	unsigned lcid_seen;
	int open_ack_seen;	/* the client acked our OPEN */
	int opened;		/* we sent OPEN */
	int stale_close;	/* a CLOSE for an unknown channel first */
	unsigned kicks;
	unsigned requests;
	uint32_t req[64];	/* the last request, words */
	unsigned req_words;
	unsigned bad_records;	/* records not padded to 8 */
} rpm;

static void
rpm_put(const uint32_t *w, unsigned n)
{
	uint32_t head = rd(RX + 4);
	unsigned i, padded = (n + 1) & ~1u;

	for (i = 0; i < padded; i++) {
		wr(RX + 8 + head, i < n ? w[i] : 0);
		head = (head + 4) % LEN;
	}
	wr(RX + 4, head);
}

static void
rpm_open(void)
{
	uint32_t w[6] = { 2 | rpm.rcid << 16, 13, 0, 0, 0, 0 };

	memcpy(&w[2], "rpm_requests", 13);
	rpm_put(w, 6);			/* 8 + ALIGN(13, 8) = 24 bytes */
	rpm.opened = 1;
}

static void
rpm_answer(uint32_t msg_id, uint32_t type, uint32_t id)
{
	uint32_t w[32], n = 0, body[24], bn = 0;
	const char *err = NULL;

	body[bn++] = 0x2367736d;	/* msg# */
	body[bn++] = 4;
	body[bn++] = msg_id;
	if (id == 99)
		err = "resource does not exist";
	else if (id == 98)
		err = "invalid key";
	if (err != NULL) {
		size_t l = strlen(err) + 1;

		body[bn++] = 0x00727265;	/* err */
		body[bn++] = (uint32_t)l;
		memset(&body[bn], 0, (l + 3) & ~3u);
		memcpy(&body[bn], err, l);
		bn += (uint32_t)((l + 3) / 4);
	}
	(void)type;
	w[n++] = 9 | rpm.rcid << 16;	/* TX_DATA */
	w[n++] = 0;
	w[n++] = 8 + 4 * bn;		/* chunk */
	w[n++] = 0;			/* left */
	w[n++] = 0x00716572;		/* req */
	w[n++] = 4 * bn;
	memcpy(&w[n], body, 4 * bn);
	n += bn;
	rpm_put(w, n);
}

/* What the client wrote since the last kick. */
static void
kick(void *ctx)
{
	uint32_t tail, head, w[64], cmd, p1, p2, len, i;

	(void)ctx;
	rpm.kicks++;
	tail = rd(TX);
	head = rd(TX + 4);
	while (tail != head) {
		/* Peek a header, then the rest of the record. */
		for (i = 0; i < 2; i++)
			w[i] = rd(TX + 8 + (tail + 4 * i) % LEN);
		cmd = w[0] & 0xffff;
		p1 = w[0] >> 16;
		p2 = w[1];
		switch (cmd) {
		case 9:			/* TX_DATA: 16 + chunk */
			for (i = 2; i < 4; i++)
				w[i] = rd(TX + 8 + (tail + 4 * i) % LEN);
			len = 16 + w[2];
			break;
		case 2:
			len = 8 + ((p2 + 7) & ~7u);
			break;
		default:
			len = 8;
		}
		len = (len + 7) & ~7u;
		for (i = 0; i < len / 4 && i < 64; i++)
			w[i] = rd(TX + 8 + (tail + 4 * i) % LEN);
		tail = (tail + len) % LEN;
		wr(TX, tail);
		if (rpm.silent)
			continue;

		switch (cmd) {
		case 0: {		/* VERSION */
			uint32_t a[2] = { 1 | 1 << 16, 0 };	/* ACK, v1 */

			CHECK(p1 == 1 && p2 == 0, "VERSION %u features %u", p1,
			    p2);
			if (rpm.stale_close) {
				uint32_t c[2] = { 3 | 77 << 16, 0 };

				rpm_put(c, 2);
				rpm.stale_close = 0;
			}
			if (rpm.send_version) {
				uint32_t v[2] = { 0 | 1 << 16, 0 };

				rpm_put(v, 2);
			}
			rpm_put(a, 2);
			if (rpm.remote_opens)
				rpm_open();
			break;
		}
		case 1:			/* VERSION_ACK to our VERSION */
			CHECK(p1 == 1 && p2 == 0, "VERSION_ACK %u %u", p1, p2);
			break;
		case 2: {		/* OPEN */
			uint32_t a[2] = { 4 | p1 << 16, 0 };

			CHECK(p2 == 13 && memcmp(&w[2], "rpm_requests", 13)
			    == 0, "OPEN name length %u", p2);
			rpm.lcid_seen = p1;
			rpm_put(a, 2);
			if (!rpm.opened)
				rpm_open();
			break;
		}
		case 4:			/* OPEN_ACK of our OPEN */
			CHECK(p1 == rpm.rcid, "OPEN_ACK for %u, ours is %u", p1,
			    rpm.rcid);
			rpm.open_ack_seen = 1;
			break;
		case 9:
			CHECK(p1 == rpm.lcid_seen && p2 == 0 && w[3] == 0,
			    "TX_DATA lcid %u iid %u left %u", p1, p2, w[3]);
			rpm.requests++;
			rpm.req_words = w[2] / 4;
			memcpy(rpm.req, &w[4], w[2]);
			rpm_answer(w[6], w[8], w[9]);
			break;
		case 13:		/* READ_NOTIF */
			break;
		default:
			CHECK(0, "unexpected command %u", cmd);
		}
	}
}

static void delay(void *ctx, unsigned us) { (void)ctx; (void)us; }

static void
setup(uint32_t ring)
{
	memset(ram, 0xa5, sizeof(ram));
	memset(&rpm, 0, sizeof(rpm));
	rpm.rcid = 0x10;
	LEN = ring;
	TX = 0x100;
	RX = 0x100 + 8 + ring + 8;
	wr(RAM - 256, 0x67727430);
	wr(RAM - 252, 2);
	wr(RAM - 248, 0x61703272);	/* ap2r */
	wr(RAM - 244, TX);
	wr(RAM - 240, ring);
	wr(RAM - 236, 0x72326170);	/* r2ap */
	wr(RAM - 232, RX);
	wr(RAM - 228, ring);
	/* The RPM's own indices: empty rings, left wherever. */
	wr(TX, 0);
	wr(RX + 4, 0);
	/* This side's, dirty: init must zero them (glink_rpm_probe()). */
	wr(TX + 4, 0x40);
	wr(RX, 0x80);
}

static void
g_setup(struct glink *g)
{
	memset(g, 0, sizeof(*g));
	g->ram = ram;
	g->ram_size = RAM;
	g->kick = kick;
	g->delay_us = delay;
}

int
main(void)
{
	static struct glink g;
	static const uint32_t swen[1] = { RPM_KEY_SWEN }, one[1] = { 1 };
	static const uint32_t k2[2] = { RPM_KEY_SWEN, RPM_KEY_UV };
	static const uint32_t v2[2] = { 1, 3000000 };
	int r;

	printf("table of contents, indices\n");
	setup(1024);
	g_setup(&g);
	CHECK(glink_init(&g) == GLINK_OK, "init");
	CHECK(g.tx_fifo == TX + 8 && g.rx_fifo == RX + 8 && g.tx_len == 1024,
	    "fifos 0x%x 0x%x len %u", g.tx_fifo, g.rx_fifo, g.tx_len);
	CHECK(rd(TX + 4) == 0 && rd(RX) == 0, "this side's indices not "
	    "zeroed: tx head %u rx tail %u", rd(TX + 4), rd(RX));
	setup(1024);
	wr(RAM - 256, 0x12345678);
	g_setup(&g);
	CHECK(glink_init(&g) == EINVAL, "bad magic accepted");
	CHECK(rd(TX + 4) == 0x40 && rd(RX) == 0x80, "wrote with a bad table");

	printf("the RPM opens the channel (as on the device)\n");
	setup(1024);
	rpm.remote_opens = 1;
	rpm.stale_close = 1;
	g_setup(&g);
	(void)glink_init(&g);
	r = glink_open(&g, "rpm_requests", 100);
	CHECK(r == GLINK_OK, "open: %d", r);
	CHECK(g.rcid == 0x10 && rpm.lcid_seen == 1 && rpm.open_ack_seen,
	    "rcid %u lcid %u ack %d", g.rcid, rpm.lcid_seen,
	    rpm.open_ack_seen);
	CHECK(g.closes == 1, "stale CLOSE: %u", g.closes);

	printf("request: l11a on, swen only (min != max)\n");
	r = glink_rpm_request(&g, RPM_TYPE_LDOA, 11, swen, one, 1, 100);
	CHECK(r == GLINK_OK, "request: %d (%s)", r, g.rpm_err);
	CHECK(rpm.req_words == 10, "request %u words, Linux sends 10",
	    rpm.req_words);
	CHECK(rpm.req[0] == 0x00716572 && rpm.req[1] == 32 &&
	    rpm.req[2] == 1 && rpm.req[3] == 0 && rpm.req[4] == 0x616f646c &&
	    rpm.req[5] == 11 && rpm.req[6] == 12 && rpm.req[7] == 0x6e657773 &&
	    rpm.req[8] == 4 && rpm.req[9] == 1,
	    "request words %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
	    rpm.req[0], rpm.req[1], rpm.req[2], rpm.req[3], rpm.req[4],
	    rpm.req[5], rpm.req[6], rpm.req[7], rpm.req[8], rpm.req[9]);

	printf("request: l3b on at 3.0 V (min == max), msg_id counts\n");
	r = glink_rpm_request(&g, RPM_TYPE_LDOB, 3, k2, v2, 2, 100);
	CHECK(r == GLINK_OK, "request: %d", r);
	CHECK(rpm.req_words == 13 && rpm.req[1] == 44 && rpm.req[2] == 2 &&
	    rpm.req[4] == 0x626f646c && rpm.req[6] == 24 &&
	    rpm.req[10] == 0x00007675 && rpm.req[11] == 4 &&
	    rpm.req[12] == 3000000, "two-key request");

	printf("the RPM refuses\n");
	r = glink_rpm_request(&g, RPM_TYPE_LDOA, 99, swen, one, 1, 100);
	CHECK(r == ENXIO && strcmp(g.rpm_err, "resource does not exist") == 0,
	    "no resource: %d '%s'", r, g.rpm_err);
	r = glink_rpm_request(&g, RPM_TYPE_LDOA, 98, swen, one, 1, 100);
	CHECK(r == EINVAL && strcmp(g.rpm_err, "invalid key") == 0,
	    "error: %d '%s'", r, g.rpm_err);

	printf("many requests through a small ring (wrap-around)\n");
	setup(96);
	rpm.remote_opens = 1;
	g_setup(&g);
	(void)glink_init(&g);
	CHECK(glink_open(&g, "rpm_requests", 100) == GLINK_OK, "open small");
	{
		unsigned i, okc = 0;

		for (i = 0; i < 40; i++)
			if (glink_rpm_request(&g, RPM_TYPE_LDOA, 1 + i % 19,
			    swen, one, 1, 100) == GLINK_OK &&
			    rpm.req[5] == 1 + i % 19)
				okc++;
		CHECK(okc == 40, "%u of 40 through a 96-byte ring", okc);
	}

	printf("the client opens first (qcom_glink_create_local())\n");
	setup(1024);
	rpm.send_version = 1;
	g_setup(&g);
	(void)glink_init(&g);
	r = glink_open(&g, "rpm_requests", 3000);
	CHECK(r == GLINK_OK && g.rcid == 0x10 && rpm.open_ack_seen,
	    "local open: %d rcid %u ack %d", r, g.rcid, rpm.open_ack_seen);

	printf("the RPM says nothing\n");
	setup(1024);
	rpm.silent = 1;
	g_setup(&g);
	(void)glink_init(&g);
	r = glink_open(&g, "rpm_requests", 50);
	CHECK(r == ETIMEDOUT, "silent RPM: %d", r);

	printf("\n%u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
