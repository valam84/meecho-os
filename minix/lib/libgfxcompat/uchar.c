/*
 * C11 mbrtoc32/c32rtomb, UTF-8 on the multibyte side whatever the locale
 * (see uchar.h for why), and the 16-bit pair over them, without surrogate
 * pairs.  For the Wayland stack (libgfxcompat).
 *
 * A sequence cut short by the end of the input is kept in the mbstate_t -
 * the bytes so far and how many - and finished by the next call, which
 * then counts only its own bytes, as C11 has it.  Overlong forms,
 * surrogates and anything past U+10FFFF are EILSEQ.
 */
#include <errno.h>
#include <string.h>
#include <wchar.h>

#include <uchar.h>

struct u8state {
	unsigned char held;		/* bytes of a sequence so far, 0-3 */
	unsigned char byte[3];
};

static mbstate_t internal32;

/* How long the sequence its first byte starts is; 0: not a first byte. */
static unsigned
u8len(unsigned char b)
{
	if (b < 0x80)
		return 1;
	if (b >= 0xc2 && b <= 0xdf)
		return 2;
	if (b >= 0xe0 && b <= 0xef)
		return 3;
	if (b >= 0xf0 && b <= 0xf4)
		return 4;
	return 0;
}

/* Whether b may follow lead as the second byte (the overlong and
 * out-of-range forms are told by the second byte). */
static int
u8second(unsigned char lead, unsigned char b)
{
	if ((b & 0xc0) != 0x80)
		return 0;
	switch (lead) {
	case 0xe0: return b >= 0xa0;	/* overlong below U+0800 */
	case 0xed: return b < 0xa0;	/* the surrogates */
	case 0xf0: return b >= 0x90;	/* overlong below U+10000 */
	case 0xf4: return b < 0x90;	/* past U+10FFFF */
	default: return 1;
	}
}

size_t
mbrtoc32(char32_t *c, const char *s, size_t n, mbstate_t *ps)
{
	struct u8state st;
	unsigned char seq[4];
	unsigned have, need, i;
	size_t took = 0;
	char32_t v;

	if (ps == NULL)
		ps = &internal32;
	if (s == NULL) {
		memset(ps, 0, sizeof(*ps));
		return 0;
	}
	memcpy(&st, ps, sizeof(st));
	if (st.held > 3)
		goto ilseq;
	have = st.held;
	memcpy(seq, st.byte, have);
	if (have == 0) {
		if (n == 0)
			return (size_t)-2;
		seq[have++] = (unsigned char)s[took++];
	}
	if ((need = u8len(seq[0])) == 0)
		goto ilseq;
	while (have < need) {
		if (took == n) {
			st.held = (unsigned char)have;
			memcpy(st.byte, seq, have);
			memcpy(ps, &st, sizeof(st));
			return (size_t)-2;
		}
		seq[have] = (unsigned char)s[took++];
		if (have == 1 ? !u8second(seq[0], seq[1]) :
		    (seq[have] & 0xc0) != 0x80)
			goto ilseq;
		have++;
	}

	v = need == 1 ? seq[0] : seq[0] & (0x7f >> need);
	for (i = 1; i < need; i++)
		v = v << 6 | (seq[i] & 0x3f);
	memset(ps, 0, sizeof(*ps));
	if (c != NULL)
		*c = v;
	return v == 0 ? 0 : took;

ilseq:
	memset(ps, 0, sizeof(*ps));
	errno = EILSEQ;
	return (size_t)-1;
}

size_t
c32rtomb(char *s, char32_t c, mbstate_t *ps)
{
	if (ps == NULL)
		ps = &internal32;
	memset(ps, 0, sizeof(*ps));	/* nothing is ever left over here */
	if (s == NULL)
		return 1;
	if (c < 0x80) {
		s[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		s[0] = (char)(0xc0 | c >> 6);
		s[1] = (char)(0x80 | (c & 0x3f));
		return 2;
	}
	if (c >= 0xd800 && c <= 0xdfff)
		goto ilseq;
	if (c < 0x10000) {
		s[0] = (char)(0xe0 | c >> 12);
		s[1] = (char)(0x80 | (c >> 6 & 0x3f));
		s[2] = (char)(0x80 | (c & 0x3f));
		return 3;
	}
	if (c < 0x110000) {
		s[0] = (char)(0xf0 | c >> 18);
		s[1] = (char)(0x80 | (c >> 12 & 0x3f));
		s[2] = (char)(0x80 | (c >> 6 & 0x3f));
		s[3] = (char)(0x80 | (c & 0x3f));
		return 4;
	}
ilseq:
	errno = EILSEQ;
	return (size_t)-1;
}

size_t
mbrtoc16(char16_t *c, const char *s, size_t n, mbstate_t *ps)
{
	char32_t c32;
	size_t r = mbrtoc32(&c32, s, n, ps);

	if (r <= n && c != NULL) {
		if (c32 > 0xffff) {
			errno = EILSEQ;		/* no surrogate pairs here */
			return (size_t)-1;
		}
		*c = (char16_t)c32;
	}
	return r;
}

size_t
c16rtomb(char *s, char16_t c, mbstate_t *ps)
{
	if (c >= 0xd800 && c <= 0xdfff) {
		errno = EILSEQ;
		return (size_t)-1;
	}
	return c32rtomb(s, c, ps);
}
