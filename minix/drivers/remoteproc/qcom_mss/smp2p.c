/*
 * smp2p (smp2p.h).  The item, packed:
 *
 *	 0  magic "$SMP" (0x504d5324)
 *	 4  version (8 bits), features (24 bits: bit 0 SSR_ACK)
 *	 8  local_pid, remote_pid (16 bits each)
 *	12  total_entries (16), valid_entries (16)
 *	16  flags
 *	20  16 entries of {name[16], value (32 bits)}
 *
 * Ours is written with version 0, then version 1 last: the remote takes an
 * item as valid by its version.  The features are what both sides have:
 * ours AND theirs, once their item's version is ours.
 */

#include "smp2p.h"

#include <string.h>

#define SMP2P_MAGIC		0x504d5324
#define SMP2P_VERSION		1
#define SMP2P_FEATURE_SSR_ACK	0x1
#define MAX_ENTRY		16
#define NAME_LEN		16

#define I_MAGIC			0
#define I_VERFEAT		4
#define I_PIDS			8
#define I_COUNTS		12
#define I_FLAGS			16
#define I_ENTRY(i)		(20 + 20 * (i))
#define ITEM_SIZE		I_ENTRY(MAX_ENTRY)

int
smp2p_init(struct smp2p *p, const char *const *names, unsigned n)
{
	struct smem *s = p->smem;
	char name[NAME_LEN];
	size_t size;
	unsigned i;

	if (n > MAX_ENTRY)
		return -1;
	if (smem_alloc(s, p->remote_pid, p->out_item, ITEM_SIZE) < 0)
		return -1;
	if ((p->out = smem_get(s, p->remote_pid, p->out_item, &size)) == 0 ||
	    size < ITEM_SIZE) {
		s->why = "our smp2p item is not there after allocating it";
		return -1;
	}

	for (i = 0; i < ITEM_SIZE; i += 4)
		smem_wr32(s, p->out + i, 0);
	smem_wr32(s, p->out + I_MAGIC, SMP2P_MAGIC);
	smem_wr32(s, p->out + I_VERFEAT, SMP2P_FEATURE_SSR_ACK << 8);
	smem_wr32(s, p->out + I_PIDS, p->local_pid | p->remote_pid << 16);
	smem_wr32(s, p->out + I_COUNTS, MAX_ENTRY | n << 16);
	for (i = 0; i < n; i++) {
		memset(name, 0, sizeof(name));
		strncpy(name, names[i], sizeof(name) - 1);
		smem_write(s, p->out + I_ENTRY(i), name, NAME_LEN);
	}
	p->nout = n;
	__sync_synchronize();
	smem_wr32(s, p->out + I_VERFEAT, SMP2P_VERSION |
	    SMP2P_FEATURE_SSR_ACK << 8);
	__sync_synchronize();
	p->kick();
	return 0;
}

int
smp2p_poll(struct smp2p *p)
{
	struct smem *s = p->smem;
	size_t size;
	uint32_t in_vf, out_vf;

	if (p->in == 0) {
		p->in = smem_get(s, p->remote_pid, p->in_item, &size);
		if (p->in == 0 || size < ITEM_SIZE ||
		    smem_rd32(s, p->in + I_MAGIC) != SMP2P_MAGIC) {
			p->in = 0;
			return 0;
		}
	}
	if (!p->negotiated) {
		in_vf = smem_rd32(s, p->in + I_VERFEAT);
		out_vf = smem_rd32(s, p->out + I_VERFEAT);
		if ((in_vf & 0xff) == (out_vf & 0xff)) {
			smem_wr32(s, p->out + I_VERFEAT, (out_vf & 0xff) |
			    ((out_vf & in_vf) & ~0xffu));
			p->negotiated = 1;
		}
	}
	return 1;
}

int
smp2p_in(struct smp2p *p, const char *name, uint32_t *v)
{
	struct smem *s = p->smem;
	char buf[NAME_LEN + 1];
	unsigned i, n;

	if (!smp2p_poll(p))
		return -1;
	n = smem_rd32(s, p->in + I_COUNTS) >> 16;
	if (n > MAX_ENTRY)
		n = MAX_ENTRY;
	for (i = 0; i < n; i++) {
		smem_read(s, p->in + I_ENTRY(i), buf, NAME_LEN);
		buf[NAME_LEN] = '\0';
		if (strcmp(buf, name) == 0) {
			*v = smem_rd32(s, p->in + I_ENTRY(i) + NAME_LEN);
			return 0;
		}
	}
	return -1;
}

void
smp2p_set(struct smp2p *p, unsigned i, uint32_t mask, uint32_t value)
{
	struct smem *s = p->smem;
	size_t off;
	uint32_t old, v;

	if (i >= p->nout)
		return;
	off = p->out + I_ENTRY(i) + NAME_LEN;
	old = smem_rd32(s, off);
	v = (old & ~mask) | (value & mask);
	smem_wr32(s, off, v);
	if (v != old) {
		__sync_synchronize();
		p->kick();
	}
}
