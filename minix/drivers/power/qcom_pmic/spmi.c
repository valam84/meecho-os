/*
 * The SPMI PMIC arbiter, versions 2 and 3 (spmi.h; port/clover/hw-spmi.md).
 *
 * The arbiter sits between the application processor and the PMICs, and
 * gives each execution environment (EE) a window of its own per peripheral:
 * a channel, numbered by APID.  A peripheral is a PPID - the slave ID and
 * the upper byte of the register address - and the arbiter's core region
 * says which APID carries which PPID.  Reads go through the "observer"
 * channels, which any EE may use for any peripheral; writes through the
 * EE's own channels, and only to peripherals the EE owns.
 *
 * A transaction: data into WDATA (writes), the command word into CMD, then
 * STATUS polled until done; a read's bytes come back in RDATA.  The command
 * carries the low address byte and the length - the slave and the upper
 * byte are the channel's.  Everything is 32-bit accesses.
 *
 * Nothing here writes outside the channel registers: not the core table,
 * not the owner table, not the interrupt region.
 */

#include <string.h>
#include <errno.h>

#include "spmi.h"

#define VERSION_REG		0x0000
#define VERSION_V2_MIN		0x20010000u
#define VERSION_V5_MIN		0x50000000u

#define CORE_CHNL_TABLE		0x0800	/* 4 * apid: PPID in bits 19:8 */
#define CNFG_OWNER_TABLE	0x0700	/* 4 * apid: owner EE in bits 2:0 */

#define CHAN_STRIDE		0x8000	/* per APID */
#define EE_STRIDE		0x1000	/* per EE, within it */

#define CH_CMD			0x00
#define CH_STATUS		0x08
#define CH_WDATA0		0x10
#define CH_WDATA1		0x14
#define CH_RDATA0		0x18
#define CH_RDATA1		0x1c

#define OPC_EXT_WRITEL		0
#define OPC_EXT_READL		1

#define ST_DONE			(1u << 0)
#define ST_FAILURE		(1u << 1)
#define ST_DENIED		(1u << 2)
#define ST_DROPPED		(1u << 3)

#define POLLS			1000	/* of 1 us: Linux's PMIC_ARB_TIMEOUT_US */

static uint32_t
rd(volatile uint8_t *base, size_t off)
{
	return *(volatile uint32_t *)(base + off);
}

static void
wr(volatile uint8_t *base, size_t off, uint32_t v)
{
	*(volatile uint32_t *)(base + off) = v;
}

int
spmi_init(struct spmi *s)
{
	unsigned n, ppid;
	uint32_t v;

	if (s->core == NULL || s->cnfg == NULL || s->obsrvr == NULL ||
	    s->chnls == NULL || s->delay_us == NULL)
		return EINVAL;

	s->version = rd(s->core, VERSION_REG);
	if (s->version < VERSION_V2_MIN || s->version >= VERSION_V5_MIN)
		return ENOTSUP;

	memset(s->apid_of, 0xff, sizeof(s->apid_of));
	memset(s->owner, 0xff, sizeof(s->owner));
	s->napid = 0;
	for (n = 0; n < SPMI_MAX_APID &&
	    CORE_CHNL_TABLE + 4 * (size_t)(n + 1) <= s->core_size; n++) {
		s->napid = n + 1;
		if ((v = rd(s->core, CORE_CHNL_TABLE + 4 * (size_t)n)) == 0)
			continue;
		ppid = (v >> 8) & 0xfff;
		if (s->apid_of[ppid] == SPMI_NO_APID)
			s->apid_of[ppid] = (uint16_t)n;
		if (CNFG_OWNER_TABLE + 4 * (size_t)(n + 1) <= s->cnfg_size)
			s->owner[n] = (uint8_t)(rd(s->cnfg, CNFG_OWNER_TABLE +
			    4 * (size_t)n) & 7);
	}
	return OK_SPMI;
}

static unsigned
apid(const struct spmi *s, unsigned sid, unsigned addr)
{
	if (sid > 15 || addr > 0xffff)
		return SPMI_NO_APID;
	return s->apid_of[(sid << 8) | ((addr >> 8) & 0xff)];
}

int
spmi_present(const struct spmi *s, unsigned sid, unsigned addr)
{
	return apid(s, sid, addr) != SPMI_NO_APID;
}

int
spmi_writable(const struct spmi *s, unsigned sid, unsigned addr)
{
	unsigned a = apid(s, sid, addr);

	return a != SPMI_NO_APID && s->owner[a] == s->ee;
}

/* The channel's registers in a region, or NULL if they would not fit. */
static volatile uint8_t *
channel(const struct spmi *s, volatile uint8_t *region, size_t size,
	unsigned a)
{
	size_t off = (size_t)a * CHAN_STRIDE + (size_t)s->ee * EE_STRIDE;

	if (a >= s->napid || off + CH_RDATA1 + 4 > size)
		return NULL;
	return region + off;
}

static int
wait_done(const struct spmi *s, volatile uint8_t *ch)
{
	uint32_t st;
	unsigned i;

	for (i = 0; i < POLLS; i++) {
		st = rd(ch, CH_STATUS);
		if (st & ST_DONE) {
			if (st & ST_FAILURE)
				return EIO;
			if (st & ST_DENIED)
				return EPERM;
			if (st & ST_DROPPED)
				return EIO;
			return OK_SPMI;
		}
		s->delay_us(1);
	}
	return ETIMEDOUT;
}

static uint32_t
cmd_word(unsigned opc, unsigned addr, unsigned len)
{
	return (uint32_t)opc << 27 | (uint32_t)(addr & 0xff) << 4 |
	    (uint32_t)((len - 1) & 7);
}

int
spmi_read(struct spmi *s, unsigned sid, unsigned addr, uint8_t *buf,
	unsigned len)
{
	volatile uint8_t *ch;
	uint32_t d0, d1;
	unsigned a, i;
	int r;

	if (len == 0 || len > 8)
		return EINVAL;
	if ((a = apid(s, sid, addr)) == SPMI_NO_APID)
		return ENODEV;
	if ((ch = channel(s, s->obsrvr, s->obsrvr_size, a)) == NULL)
		return ENODEV;

	wr(ch, CH_CMD, cmd_word(OPC_EXT_READL, addr, len));
	if ((r = wait_done(s, ch)) != OK_SPMI)
		return r;
	d0 = rd(ch, CH_RDATA0);
	d1 = len > 4 ? rd(ch, CH_RDATA1) : 0;
	for (i = 0; i < len; i++)
		buf[i] = (uint8_t)((i < 4 ? d0 >> (8 * i) : d1 >> (8 * (i - 4)))
		    & 0xff);
	return OK_SPMI;
}

int
spmi_write(struct spmi *s, unsigned sid, unsigned addr, const uint8_t *buf,
	unsigned len)
{
	volatile uint8_t *ch;
	uint32_t d0 = 0, d1 = 0;
	unsigned a, i;

	if (len == 0 || len > 8)
		return EINVAL;
	if ((a = apid(s, sid, addr)) == SPMI_NO_APID)
		return ENODEV;
	if (s->owner[a] != s->ee)
		return EPERM;
	if ((ch = channel(s, s->chnls, s->chnls_size, a)) == NULL)
		return ENODEV;

	for (i = 0; i < len; i++) {
		if (i < 4)
			d0 |= (uint32_t)buf[i] << (8 * i);
		else
			d1 |= (uint32_t)buf[i] << (8 * (i - 4));
	}
	wr(ch, CH_WDATA0, d0);
	if (len > 4)
		wr(ch, CH_WDATA1, d1);
	wr(ch, CH_CMD, cmd_word(OPC_EXT_WRITEL, addr, len));
	return wait_done(s, ch);
}
