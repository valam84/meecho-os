/*
 * Host test for minix/drivers/hid/qcom_touch/qup.c against a model of the
 * QUP v2 I2C master in FIFO mode (port/clover/hw-touch.md, part C) with one
 * slave on the bus: a register file at 0x38 whose pointer the first byte of
 * a write sets and reads advance - an FT5x06 as far as the driver sees.
 *
 * The model keeps the byte stream the driver puts in the output FIFO and
 * runs it when the core goes to RUN: START + address, DATA_WR n bytes,
 * DATA_RD(_STOP) n bytes; reads come back with two tag bytes in front.  It
 * checks what Linux's FIFO path does: the counters, the clock word, the
 * tags, the order.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#include "qup.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", \
    __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* The model. */
static uint32_t state, op, i2c_status, mast_gen, clk_ctl, mx_write, mx_read;
static uint32_t config;
static uint8_t outq[256], inq[256];
static unsigned outn, inn, inpos;
static uint8_t slave_regs[256], slave_ptr;
static unsigned slave_addr = 0x38;
static int dead;			/* never finish a read */
static char trace[1024];

static void
tracef(const char *s)
{
	strncat(trace, s, sizeof(trace) - strlen(trace) - 1);
}

static void
run_stream(void)
{
	unsigned i = 0, a, n, k;
	char t[64];

	/*
	 * The core takes MX_WRITE_CNT bytes of the stream and no more: the
	 * rest of the last word written is padding, as in Linux's FIFO path.
	 */
	if (outn > (mx_write & 0xffff))
		outn = mx_write & 0xffff;
	while (i < outn) {
		uint8_t tag = outq[i++];

		if (tag == 0x81) {
			a = outq[i++];
			snprintf(t, sizeof(t), "S%02x ", a);
			tracef(t);
			if ((a >> 1) != slave_addr) {
				i2c_status |= 1u << 3;	/* NACK */
				outn = 0;
				return;
			}
			continue;
		}
		if (tag == 0x82 || tag == 0x83) {
			n = outq[i++];
			if (n == 0)
				n = 256;
			snprintf(t, sizeof(t), "W%u ", n);
			tracef(t);
			for (k = 0; k < n; k++) {
				if (k == 0)
					slave_ptr = outq[i];
				i++;
			}
			op |= (1u << 8) | (1u << 10);
			if (tag == 0x83)
				tracef("P ");
			continue;
		}
		if (tag == 0x85 || tag == 0x86 || tag == 0x87) {
			n = outq[i++];
			snprintf(t, sizeof(t), "R%u%s ", n, tag == 0x87 ? "P" :
			    "");
			tracef(t);
			if (dead)
				continue;
			inq[inn++] = 0x00;		/* two rx tags */
			inq[inn++] = 0x00;
			for (k = 0; k < n; k++)
				inq[inn++] = slave_regs[slave_ptr++];
			op |= 1u << 9;
			continue;
		}
		snprintf(t, sizeof(t), "?%02x ", tag);
		tracef(t);
		i2c_status |= 1u << 4;	/* some bus error bit */
	}
	outn = 0;
}

static uint32_t
mrd(void *ctx, uint32_t off)
{
	uint32_t w = 0;
	unsigned k;

	(void)ctx;
	switch (off) {
	case 0x004:
		return state | 4 | (mast_gen ? 0x10 : 0);
	case 0x008:	/* blocks of 16, FIFOs of 64 each way */
		return 1 | 1 << 2 | 1 << 5 | 1 << 7;
	case 0x018:
		return op;
	case 0x01c:
		return 0;
	case 0x030:
		return 0x20020000;
	case 0x218:
		for (k = 0; k < 4; k++)
			if (inpos < inn)
				w |= (uint32_t)inq[inpos++] << (8 * k);
		return w;
	case 0x404:
		return i2c_status;
	}
	return 0;
}

static void
mwr(void *ctx, uint32_t off, uint32_t v)
{
	char t[48];
	unsigned k;

	(void)ctx;
	switch (off) {
	case 0x000:
		config = v;
		break;
	case 0x004:
		state = v & 3;
		if (state == 1) {
			snprintf(t, sizeof(t), "[run w%x r%x c%x] ", mx_write,
			    mx_read, config);
			tracef(t);
			run_stream();
		}
		if (state == 0) {
			inn = inpos = 0;
			outn = 0;
		}
		break;
	case 0x00c:
		state = 0;
		mast_gen = 0;
		op = 0;
		inn = inpos = outn = 0;
		break;
	case 0x018:
		op &= ~v;
		break;
	case 0x110:
		for (k = 0; k < 4; k++)
			outq[outn++] = (uint8_t)(v >> (8 * k));
		break;
	case 0x150:
		mx_write = v;
		break;
	case 0x208:
		mx_read = v;
		break;
	case 0x400:
		clk_ctl = v;
		break;
	case 0x404:
		i2c_status &= ~v;
		break;
	case 0x408:
		mast_gen = v & 1;
		break;
	}
}

static unsigned delays;
static void
delay_us(unsigned us)
{
	(void)us;
	delays++;
}

int
main(void)
{
	struct qup q;
	uint8_t buf[64];
	unsigned i;
	int r;

	memset(&q, 0, sizeof(q));
	q.rd = mrd;
	q.wr = mwr;
	q.delay_us = delay_us;
	q.src_hz = 19200000;
	q.bus_hz = 400000;
	CHECK(qup_init(&q) == QUP_OK, "init");
	CHECK(q.clk_ctl == 0x000e031c, "clk_ctl %08x", q.clk_ctl);
	CHECK(q.out_fifo == 64 && q.in_fifo == 64, "fifos %u/%u", q.out_fifo,
	    q.in_fifo);

	/* A frame: 33 bytes from register 0. */
	for (i = 0; i < 256; i++)
		slave_regs[i] = (uint8_t)(i * 7 + 3);
	trace[0] = 0;
	r = qup_read_reg(&q, 0x38, 0x00, buf, 33);
	CHECK(r == QUP_OK, "read: %d", r);
	CHECK(memcmp(buf, slave_regs, 33) == 0, "frame wrong");
	CHECK(clk_ctl == 0x000e031c, "CLK_CTL written %08x", clk_ctl);
	/* RUN once for the clock word, once with the FIFO filled. */
	CHECK(strcmp(trace, "[run w5 r0 c287] [run w5 r0 c287] S70 W1 "
	    "[run w80000004 r80000023 c207] S71 R33P ") == 0, "trace: %s",
	    trace);

	/* Another register, odd lengths: 1, 2, 3, 22, 62. */
	{
		static const unsigned lens[] = { 1, 2, 3, 22, 62 };
		for (i = 0; i < 5; i++) {
			memset(buf, 0, sizeof(buf));
			r = qup_read_reg(&q, 0x38, 0xbb, buf, lens[i]);
			CHECK(r == QUP_OK && memcmp(buf, &slave_regs[0xbb],
			    lens[i]) == 0, "read %u at 0xbb", lens[i]);
		}
	}
	CHECK(qup_read_reg(&q, 0x38, 0, buf, 63) == EINVAL, "63 accepted");
	CHECK(qup_read_reg(&q, 0x38, 0, buf, 0) == EINVAL, "0 accepted");
	CHECK(qup_read_reg(&q, 0x80, 0, buf, 1) == EINVAL, "addr 0x80");

	/* Nobody at 0x39: NACK, cleared, and the next read works. */
	r = qup_read_reg(&q, 0x39, 0, buf, 4);
	CHECK(r == ENXIO, "nack gave %d", r);
	CHECK((i2c_status & 0x038000fc) == 0, "error left set");
	r = qup_read_reg(&q, 0x38, 0x10, buf, 4);
	CHECK(r == QUP_OK && memcmp(buf, &slave_regs[0x10], 4) == 0,
	    "read after nack");

	/* A read that never completes: bounded. */
	dead = 1;
	delays = 0;
	r = qup_read_reg(&q, 0x38, 0, buf, 4);
	CHECK(r == ETIMEDOUT, "dead read gave %d", r);
	CHECK(delays >= 10000 && delays < 20000, "waited %u", delays);
	dead = 0;
	r = qup_read_reg(&q, 0x38, 0x20, buf, 8);
	CHECK(r == QUP_OK && memcmp(buf, &slave_regs[0x20], 8) == 0,
	    "read after timeout");

	printf("%s: %d failure%s\n", fails ? "FAIL" : "ok", fails,
	    fails == 1 ? "" : "s");
	return fails != 0;
}
