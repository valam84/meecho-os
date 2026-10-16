/*
 * GLINK over the RPM's message RAM, and the RPM's request protocol on top
 * of it (glink.c) - the client half of Linux's qcom_glink_rpm.c,
 * qcom_glink_native.c (intentless) and smd-rpm.c, polled.
 *
 * No MINIX calls: the message RAM is a pointer to mapped memory, and the
 * kick (the APCS IPC bit) and the delay are the caller's, so the host stand
 * (port/test/qcom_rpm/) runs this file against a model of the RPM.
 */
#ifndef _QCOM_GLINK_H
#define _QCOM_GLINK_H

#include <stddef.h>
#include <stdint.h>

#define GLINK_OK		0

/* RPM resource types (include/linux/soc/qcom/smd-rpm.h). */
#define RPM_TYPE_SMPA		0x61706d73	/* "smpa" */
#define RPM_TYPE_SMPB		0x62706d73
#define RPM_TYPE_LDOA		0x616f646c	/* "ldoa" */
#define RPM_TYPE_LDOB		0x626f646c
#define RPM_TYPE_BOBB		0x62626f62
#define RPM_TYPE_RWCX		0x78637772
#define RPM_TYPE_RWMX		0x786d7772
#define RPM_TYPE_RWLC		0x636c7772
#define RPM_TYPE_RWLM		0x6d6c7772

/* Request keys (qcom_smd-regulator.c). */
#define RPM_KEY_SWEN		0x6e657773	/* "swen" */
#define RPM_KEY_UV		0x00007675	/* "uv" */

struct glink {
	/* The caller's. */
	volatile uint8_t *ram;
	size_t		ram_size;
	void		(*kick)(void *ctx);
	void		(*delay_us)(void *ctx, unsigned us);
	void		*ctx;

	/* The two FIFOs, as offsets into ram (glink_init()). */
	uint32_t	tx_tail, tx_head, tx_fifo, tx_len;	/* ap2r */
	uint32_t	rx_tail, rx_head, rx_fifo, rx_len;	/* r2ap */
	uint32_t	found[4];	/* tx tail, tx head, rx tail, rx head */

	/* The link and the one channel. */
	uint32_t	features;
	int		version_sent, version_ok;
	unsigned	lcid, rcid;
	int		open_sent, open_acked, remote_ack_sent;
	int		read_notify_sent;

	/* The last data on the channel. */
	uint8_t		reply[256];
	unsigned	reply_len;
	int		reply_done;

	/* What went by: for the log. */
	unsigned	stray;			/* data for other channels */
	unsigned	closes;			/* CLOSE for unknown channels */
	char		rpm_err[48];		/* the RPM's "err" text */
};

/*
 * Read the table of contents at the end of the message RAM and reset this
 * side's indices (tx head, rx tail), as glink_rpm_probe() does.  EINVAL for
 * a missing magic or FIFOs; nothing is written then.
 */
int glink_init(struct glink *g);

/*
 * Version negotiation and the channel: VERSION, then the opening dance
 * whichever side starts it (qcom_glink_create_remote() or _create_local()).
 * ETIMEDOUT if the RPM does not answer within timeout_ms, EPROTO for a
 * command this client cannot step over.
 */
int glink_open(struct glink *g, const char *name, unsigned timeout_ms);

/*
 * One RPM request for the active set (qcom_rpm_smd_write()): resource type
 * and id, then nkv key-value pairs of 32 bits each.  OK when the RPM's
 * answer carries no "err"; EINVAL with the text in rpm_err if it does;
 * ENXIO for "resource does not exist" (as Linux).
 */
int glink_rpm_request(struct glink *g, uint32_t type, uint32_t id,
	const uint32_t *keys, const uint32_t *values, unsigned nkv,
	unsigned timeout_ms);

#endif /* _QCOM_GLINK_H */
