/*
 * The Qualcomm QUP v2 as an I2C master, FIFO mode, polled (qup.c).
 *
 * Register access goes through rd/wr so that the host test
 * (port/test/qcom_touch/) sees every word written to the FIFO; on the
 * machine they are plain loads and stores on the mapped block.  No MINIX
 * calls: the caller supplies the delay.  port/clover/hw-touch.md, part C.
 */
#ifndef _QUP_H
#define _QUP_H

#include <stdint.h>

#define QUP_OK		0

struct qup {
	uint32_t	(*rd)(void *ctx, uint32_t off);
	void		(*wr)(void *ctx, uint32_t off, uint32_t v);
	void		(*delay_us)(unsigned us);
	void		*ctx;

	uint32_t	src_hz;		/* the core clock: 19.2 MHz */
	uint32_t	bus_hz;		/* 400 kHz */

	/* Filled by qup_init(). */
	uint32_t	clk_ctl;
	unsigned	out_fifo, in_fifo;	/* bytes */
	uint32_t	hw_version;
};

int qup_init(struct qup *q);

/*
 * Write one register number to 7-bit slave addr, then - repeated start -
 * read n bytes back.  n is at most in_fifo - 2.  ENXIO for a slave that
 * does not acknowledge, EIO for a bus error, ETIMEDOUT for a controller
 * that does not finish.
 */
int qup_read_reg(struct qup *q, unsigned addr, uint8_t reg, uint8_t *buf,
	unsigned n);

#endif /* _QUP_H */
