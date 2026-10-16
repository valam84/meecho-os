/*
 * A client of the Qualcomm SPMI PMIC arbiter, versions 2 and 3: register
 * reads through the observer channels and writes through this EE's own.
 *
 * Arithmetic over mapped registers and nothing else - no MINIX calls, the
 * delay is the caller's - so the host test (port/test/qcom_pmic/) runs this
 * file against a model of the arbiter.  The layout is port/clover/hw-spmi.md.
 */
#ifndef _QCOM_SPMI_H
#define _QCOM_SPMI_H

#include <stddef.h>
#include <stdint.h>

#define OK_SPMI			0	/* success; the file builds on a host too */
#define SPMI_MAX_APID		512	/* core + 0x800 .. + 0xfff */
#define SPMI_NO_APID		0xffff

struct spmi {
	/* Mapped by the caller; sizes are what the device tree says. */
	volatile uint8_t *core, *cnfg, *obsrvr, *chnls;
	size_t core_size, cnfg_size, obsrvr_size, chnls_size;
	unsigned ee;			/* qcom,ee: ours is 0 */
	void (*delay_us)(unsigned us);

	/* Filled by spmi_init(). */
	uint32_t version;
	unsigned napid;			/* entries the core table has room for */
	uint16_t apid_of[4096];		/* by PPID; SPMI_NO_APID if none */
	uint8_t owner[SPMI_MAX_APID];	/* the EE that owns each APID */
};

/*
 * Check the version (v2 or v3 only; anything else is refused, since the
 * offsets below are theirs) and read the PPID and owner tables.  Writes
 * nothing.
 */
int spmi_init(struct spmi *s);

/*
 * len bytes (1..8) at (sid, addr).  A write goes only to a peripheral this
 * EE owns: the arbiter would deny another EE's channel anyway, and asking
 * it is how a mistake reaches somebody else's hardware.
 */
int spmi_read(struct spmi *s, unsigned sid, unsigned addr, uint8_t *buf,
	unsigned len);
int spmi_write(struct spmi *s, unsigned sid, unsigned addr,
	const uint8_t *buf, unsigned len);

/* Whether (sid, addr)'s peripheral is mapped, and whether we may write it. */
int spmi_present(const struct spmi *s, unsigned sid, unsigned addr);
int spmi_writable(const struct spmi *s, unsigned sid, unsigned addr);

#endif /* _QCOM_SPMI_H */
