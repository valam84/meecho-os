/*
 * The QUP v2 I2C master, FIFO mode, polled (qup.h).
 *
 * A transfer is a stream of bytes in the output FIFO: tags that say what the
 * I2C engine is to do (START with the address byte, then write or read N
 * with or without a STOP after) and the data to write.  The input FIFO
 * gives back two tag bytes and the data read.  The core runs through a
 * state machine - RESET, RUN, PAUSE - and a register changes state only
 * once the previous change is "valid".  Write-then-read is done as Linux's
 * FIFO path does it, in two sub-transfers: the register number with no STOP
 * (so the bus is held), then the read with a STOP; the second carries the
 * "configured while running" bit in its counters.
 *
 * Errors - a NACK, arbitration lost, anything in the error flags - are
 * cleared by writing them back, the core is put into RESET, and the next
 * transfer starts from qup_init()'s reset again.
 */

#include <errno.h>
#include <string.h>

#include "qup.h"

#define CONFIG			0x000
#define STATE			0x004
#define IO_MODE			0x008
#define SW_RESET		0x00c
#define OPERATIONAL		0x018
#define ERROR_FLAGS		0x01c
#define HW_VERSION		0x030
#define MX_OUTPUT_CNT		0x100
#define OUT_FIFO		0x110
#define MX_WRITE_CNT		0x150
#define MX_INPUT_CNT		0x200
#define MX_READ_CNT		0x208
#define IN_FIFO			0x218
#define I2C_CLK_CTL		0x400
#define I2C_STATUS		0x404
#define I2C_MASTER_GEN		0x408

#define ST_RESET		0
#define ST_RUN			1
#define ST_PAUSE		3
#define ST_MASK			3
#define ST_VALID		(1u << 2)
#define ST_MAST_GEN		(1u << 4)

#define CFG_I2C			0x207	/* mini-core I2C, 8-bit words */
#define CFG_NO_INPUT		(1u << 7)

#define IO_FIFO_PACK		0xc000	/* FIFO both ways, pack/unpack */

#define OP_OUT_SVC		(1u << 8)
#define OP_IN_SVC		(1u << 9)
#define OP_OUTPUT_DONE		(1u << 10)

#define I2C_NACK		(1u << 3)
#define I2C_BUS_ACTIVE		(1u << 8)
#define I2C_ERR_MASK		0x038000fcu
#define QUP_ERR_MASK		0x7cu

#define CNT_RUNNING		0x80000000u

#define TAG_START		0x81
#define TAG_DATA_WR		0x82
#define TAG_DATA_RD_STOP	0x87

#define POLLS			10000	/* of 1 us */

static uint32_t
rd(struct qup *q, uint32_t off)
{
	return q->rd(q->ctx, off);
}

static void
wr(struct qup *q, uint32_t off, uint32_t v)
{
	q->wr(q->ctx, off, v);
}

static int
wait_state(struct qup *q, uint32_t want_mask, uint32_t want)
{
	unsigned i;

	for (i = 0; i < POLLS; i++) {
		if ((rd(q, STATE) & want_mask) == want)
			return QUP_OK;
		q->delay_us(1);
	}
	return ETIMEDOUT;
}

static int
set_state(struct qup *q, uint32_t s)
{
	int r;

	if ((r = wait_state(q, ST_VALID, ST_VALID)) != QUP_OK)
		return r;
	wr(q, STATE, s);
	return wait_state(q, ST_VALID | ST_MASK, ST_VALID | s);
}

/* Errors so far: cleared, the core reset, the code for them. */
static int
check_errors(struct qup *q)
{
	uint32_t be = rd(q, I2C_STATUS) & I2C_ERR_MASK;
	uint32_t qe = rd(q, ERROR_FLAGS) & QUP_ERR_MASK;

	if (be == 0 && qe == 0)
		return QUP_OK;
	if (be)
		wr(q, I2C_STATUS, be);
	if (qe)
		wr(q, ERROR_FLAGS, qe);
	wr(q, STATE, ST_RESET);
	return (be & I2C_NACK) ? ENXIO : EIO;
}

/* Wait for any of the OPERATIONAL bits in mask, watching for errors. */
static int
wait_op(struct qup *q, uint32_t mask, uint32_t *got)
{
	uint32_t op;
	unsigned i;
	int r;

	for (i = 0; i < POLLS; i++) {
		if ((r = check_errors(q)) != QUP_OK)
			return r;
		op = rd(q, OPERATIONAL);
		if (op & mask) {
			*got = op;
			return QUP_OK;
		}
		q->delay_us(1);
	}
	return ETIMEDOUT;
}

static int
reset_core(struct qup *q)
{
	int r;

	wr(q, SW_RESET, 1);
	if ((r = wait_state(q, ST_VALID | ST_MASK, ST_VALID | ST_RESET)) !=
	    QUP_OK)
		return r;
	wr(q, CONFIG, CFG_I2C);
	wr(q, I2C_MASTER_GEN, 1);
	return wait_state(q, ST_VALID | ST_MAST_GEN, ST_VALID | ST_MAST_GEN);
}

int
qup_init(struct qup *q)
{
	static const unsigned blk[4] = { 4, 16, 32, 0 };
	uint32_t io, fs_div, hs_div = 3;
	int r;

	if (q->rd == NULL || q->wr == NULL || q->delay_us == NULL ||
	    q->bus_hz == 0 || q->src_hz < q->bus_hz * 8)
		return EINVAL;

	if ((r = reset_core(q)) != QUP_OK)
		return r;
	q->hw_version = rd(q, HW_VERSION);

	/* FIFO sizes, read before IO_MODE is ever written. */
	io = rd(q, IO_MODE);
	q->out_fifo = blk[io & 3] * (2u << ((io >> 2) & 7));
	q->in_fifo = blk[(io >> 5) & 3] * (2u << ((io >> 7) & 7));
	if (q->out_fifo < 8 || q->in_fifo < 8)
		return EIO;

	/* Fast mode: fs_div = (src/bus - 6) * 2/3, hs_div 3. */
	fs_div = ((q->src_hz / q->bus_hz) - 6) * 2 / 3;
	q->clk_ctl = (fs_div / 2) << 16 | hs_div << 8 | (fs_div & 0xff);
	return QUP_OK;
}

int
qup_read_reg(struct qup *q, unsigned addr, uint8_t reg, uint8_t *buf,
	unsigned n)
{
	uint32_t got, w, word;
	unsigned i, have, words;
	int r;

	if (addr > 0x7f || n == 0 || n + 2 > q->in_fifo || n > 254)
		return EINVAL;

	if ((r = reset_core(q)) != QUP_OK)
		return r;
	wr(q, MX_OUTPUT_CNT, 0);
	wr(q, MX_INPUT_CNT, 0);
	wr(q, IO_MODE, IO_FIFO_PACK);

	/* 1. The register number: START, address (write), one byte, no STOP. */
	wr(q, MX_WRITE_CNT, 5);
	wr(q, CONFIG, CFG_I2C | CFG_NO_INPUT);
	if ((r = set_state(q, ST_RUN)) != QUP_OK)
		goto fail;
	wr(q, I2C_CLK_CTL, q->clk_ctl);
	if ((r = set_state(q, ST_PAUSE)) != QUP_OK)
		goto fail;
	wr(q, OUT_FIFO, (uint32_t)TAG_START | (uint32_t)(addr << 1) << 8 |
	    (uint32_t)TAG_DATA_WR << 16 | 1u << 24);
	wr(q, OUT_FIFO, reg);
	if ((r = set_state(q, ST_RUN)) != QUP_OK)
		goto fail;
	if ((r = wait_op(q, OP_OUT_SVC | OP_OUTPUT_DONE, &got)) != QUP_OK)
		goto fail;
	wr(q, OPERATIONAL, got & (OP_OUT_SVC | OP_OUTPUT_DONE));
	if ((r = set_state(q, ST_PAUSE)) != QUP_OK)
		goto fail;

	/* 2. START, address (read), read n with STOP: repeated start. */
	wr(q, MX_WRITE_CNT, CNT_RUNNING | 4);
	wr(q, MX_READ_CNT, CNT_RUNNING | (n + 2));
	wr(q, CONFIG, CFG_I2C);
	wr(q, OUT_FIFO, (uint32_t)TAG_START | (uint32_t)(addr << 1 | 1) << 8 |
	    (uint32_t)TAG_DATA_RD_STOP << 16 | (uint32_t)n << 24);
	if ((r = set_state(q, ST_RUN)) != QUP_OK)
		goto fail;
	if ((r = wait_op(q, OP_IN_SVC, &got)) != QUP_OK)
		goto fail;

	/* Two tag bytes first, then the data, little-endian in each word. */
	words = (n + 2 + 3) / 4;
	for (i = 0, have = 0; i < words; i++) {
		word = rd(q, IN_FIFO);
		for (w = 0; w < 4; w++, have++)
			if (have >= 2 && have - 2 < n)
				buf[have - 2] = (uint8_t)(word >> (8 * w));
	}
	wr(q, OPERATIONAL, OP_IN_SVC);

	/* The bus let go, then the core back to RESET. */
	for (i = 0; i < POLLS && (rd(q, I2C_STATUS) & I2C_BUS_ACTIVE); i++)
		q->delay_us(1);
	if ((r = check_errors(q)) != QUP_OK)
		return r;
	(void)set_state(q, ST_RESET);
	return QUP_OK;

fail:
	if (r == ETIMEDOUT)
		wr(q, SW_RESET, 1);
	return r;
}
