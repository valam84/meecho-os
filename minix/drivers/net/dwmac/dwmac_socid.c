/*
 * The station address, derived from the SoC's identifier in the OTP.
 *
 * The controller holds no address of its own, so somebody has to decide
 * what this board is called on the network.  Inventing one is easy and
 * wrong in a way that shows up much later: an address that changes between
 * boots, or differs from the one the same board has under its vendor
 * system, breaks every lease, every ARP cache and every firewall rule that
 * names it.  So the address is not invented here.  It is computed the way
 * the system that works on this board computes it, from a value that is
 * burnt into the chip and is therefore the same on every boot and unique
 * to this piece of silicon.
 *
 * That way is rk_get_eth_addr_from_otp() of the vendor kernel
 * (bigtreetech/linux-rockchip, branch bpi-rk-6.1-rkr5.1-rev2,
 * drivers/net/ethernet/stmicro/stmmac/dwmac-rk.c): hash the sixteen bytes
 * the device tree calls the "soc-id" cell twice, with two fixed seeds, and
 * take six bytes out of the two results.  The hash is Bob Jenkins'
 * lookup3, which its author placed in the public domain; the arrangement
 * below is the one Linux's jhash() uses, because a different arrangement -
 * however reasonable - would give this board a different address from the
 * one the network already knows it by, and then the whole exercise would
 * have been pointless.
 *
 * It is checked, not assumed: port/test/dwmac/socidtest.c feeds it the
 * sixteen bytes read out of this board's OTP under the vendor system and
 * expects the address that system reports, 8a:6e:41:09:8e:2f.  That check
 * was written and passing before this file was ever run on the board.
 */

#include "dwmac_socid.h"

#define JHASH_INITVAL	0xdeadbeefu

static uint32_t
rol32(uint32_t v, unsigned n)
{
	return (uint32_t)((v << n) | (v >> (32 - n)));
}

/*
 * Four bytes, least significant first.  Linux reads them with a native
 * load, which on every machine this SoC ships in amounts to the same
 * thing; spelling it out keeps the answer the same when the host compiler
 * that runs the test is not the target.
 */
static uint32_t
get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

#define JHASH_MIX(a, b, c)						\
	do {								\
		a -= c;  a ^= rol32(c, 4);  c += b;			\
		b -= a;  b ^= rol32(a, 6);  a += c;			\
		c -= b;  c ^= rol32(b, 8);  b += a;			\
		a -= c;  a ^= rol32(c, 16); c += b;			\
		b -= a;  b ^= rol32(a, 19); a += c;			\
		c -= b;  c ^= rol32(b, 4);  b += a;			\
	} while (0)

#define JHASH_FINAL(a, b, c)						\
	do {								\
		c ^= b; c -= rol32(b, 14);				\
		a ^= c; a -= rol32(c, 11);				\
		b ^= a; b -= rol32(a, 25);				\
		c ^= b; c -= rol32(b, 16);				\
		a ^= c; a -= rol32(c, 4);				\
		b ^= a; b -= rol32(a, 14);				\
		c ^= b; c -= rol32(b, 24);				\
	} while (0)

static uint32_t
jhash(const uint8_t *k, size_t length, uint32_t initval)
{
	uint32_t a, b, c;
	size_t left = length;

	a = b = c = JHASH_INITVAL + (uint32_t)length + initval;

	while (left > 12) {
		a += get_le32(k);
		b += get_le32(k + 4);
		c += get_le32(k + 8);
		JHASH_MIX(a, b, c);
		left -= 12;
		k += 12;
	}

	/*
	 * The tail, longest case first and every case falling into the next,
	 * which is how the original is written and why the byte that lands
	 * in which word is not obvious from any one line.
	 */
	switch (left) {
	case 12: c += (uint32_t)k[11] << 24;	/* FALLTHROUGH */
	case 11: c += (uint32_t)k[10] << 16;	/* FALLTHROUGH */
	case 10: c += (uint32_t)k[9] << 8;	/* FALLTHROUGH */
	case 9:  c += (uint32_t)k[8];		/* FALLTHROUGH */
	case 8:  b += (uint32_t)k[7] << 24;	/* FALLTHROUGH */
	case 7:  b += (uint32_t)k[6] << 16;	/* FALLTHROUGH */
	case 6:  b += (uint32_t)k[5] << 8;	/* FALLTHROUGH */
	case 5:  b += (uint32_t)k[4];		/* FALLTHROUGH */
	case 4:  a += (uint32_t)k[3] << 24;	/* FALLTHROUGH */
	case 3:  a += (uint32_t)k[2] << 16;	/* FALLTHROUGH */
	case 2:  a += (uint32_t)k[1] << 8;	/* FALLTHROUGH */
	case 1:
		a += (uint32_t)k[0];
		JHASH_FINAL(a, b, c);
		break;
	case 0:
		break;			/* nothing left to add */
	}

	return c;
}

/*
 * Six bytes out of two hashes, then the two bits that say what kind of
 * address this is: not a multicast one (bit 0 of the first byte clear),
 * and not one anybody handed out (bit 1 set - locally administered).  The
 * second is the honest half: nobody bought this address, and an address
 * that claims otherwise would collide with a real one some day.
 *
 * Fails on an identifier too short to be one, and on an identifier that is
 * all zeroes.  The second is the one worth having: a blank OTP hashes to a
 * perfectly valid-looking address, and every board with a blank OTP would
 * get the same one - two machines answering to one address, which is a far
 * worse failure than having no address at all.  The check belongs to the
 * identifier and not to the address it produces: the address always comes
 * out valid by construction, so checking that would check nothing.
 */
int
dwmac_hwaddr_from_soc_id(const uint8_t *id, size_t len, uint8_t *addr)
{
	uint32_t h1, h2;
	unsigned bits;
	size_t i;

	if (id == NULL || addr == NULL || len < DWMAC_SOC_ID_MIN)
		return -1;

	for (i = 0, bits = 0; i < len; i++)
		bits |= id[i];
	if (bits == 0)
		return -1;

	h1 = jhash(id, len, 0x35660001u);
	h2 = jhash(id, len, 0x35660002u);

	addr[0] = (uint8_t)(h1 & 0xff);
	addr[1] = (uint8_t)((h1 >> 8) & 0xff);
	addr[2] = (uint8_t)((h1 >> 16) & 0xff);
	addr[3] = (uint8_t)((h1 >> 24) & 0xff);
	addr[4] = (uint8_t)(h2 & 0xff);
	addr[5] = (uint8_t)((h2 >> 8) & 0xff);

	addr[0] &= 0xfe;
	addr[0] |= 0x02;

	return 0;
}
