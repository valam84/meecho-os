/*
 * port/gfx/compat/uchar.c against glibc's mbrtoc32/c32rtomb in C.UTF-8,
 * on the host:
 *
 *   cc -O2 -c -Dmbrtoc32=my_mbrtoc32 -Dc32rtomb=my_c32rtomb \
 *       -Dmbrtoc16=my_mbrtoc16 -Dc16rtomb=my_c16rtomb compat/uchar.c
 *   cc -O2 test/uchartest.c uchar.o && ./a.out
 *
 * Every code point is encoded by both and decoded back whole and a byte
 * at a time; then random byte strings - valid, cut short and invalid - are
 * decoded by both, the results and return values compared call by call.
 */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>

size_t my_mbrtoc32(char32_t *, const char *, size_t, mbstate_t *);
size_t my_c32rtomb(char *, char32_t, mbstate_t *);

static long checks, bad;

static void
check(int ok, const char *what, unsigned long v)
{
	checks++;
	if (!ok && bad++ < 20)
		printf("FAIL %s 0x%lx\n", what, v);
}

int
main(void)
{
	char a[8], b[8];
	unsigned long cp;
	size_t ra, rb, i;
	char32_t ca, cb;
	mbstate_t sa, sb;
	unsigned seed = 1;
	int round;

	if (setlocale(LC_CTYPE, "C.UTF-8") == NULL) {
		printf("no C.UTF-8 on the host\n");
		return 2;
	}
	for (cp = 0; cp < 0x110010; cp++) {
		memset(&sa, 0, sizeof(sa));
		memset(&sb, 0, sizeof(sb));
		ra = my_c32rtomb(a, (char32_t)cp, &sa);
		rb = c32rtomb(b, (char32_t)cp, &sb);
		if (cp > 0x10ffff) {
			/* RFC 3629 ends there; glibc goes on to 0x7fffffff */
			check(ra == (size_t)-1, "encode past U+10FFFF", cp);
			continue;
		}
		check(ra == rb && (ra == (size_t)-1 || memcmp(a, b, ra) == 0),
		    "encode", cp);
		if (ra == (size_t)-1)
			continue;
		memset(&sa, 0, sizeof(sa));
		check(my_mbrtoc32(&ca, a, ra, &sa) == (cp ? ra : 0) && ca == cp,
		    "decode whole", cp);
		/* a byte at a time: -2 until the last */
		memset(&sa, 0, sizeof(sa));
		for (i = 0; i + 1 < ra; i++)
			check(my_mbrtoc32(&ca, a + i, 1, &sa) == (size_t)-2,
			    "decode cut", cp);
		check(my_mbrtoc32(&ca, a + i, 1, &sa) == (cp ? 1 : 0) &&
		    ca == cp, "decode last byte", cp);
	}
	for (round = 0; round < 2000000; round++) {
		unsigned char buf[6];
		size_t n = 1 + (unsigned)(rand_r(&seed) % 6), off;

		for (i = 0; i < n; i++) {
			unsigned r = (unsigned)rand_r(&seed);
			/* lean on the bytes that matter */
			buf[i] = (r & 3) == 0 ? (unsigned char)(0x80 | (r >> 8 & 0x3f)) :
			    (r & 3) == 1 ? (unsigned char)(0xc0 | (r >> 8 & 0x3f)) :
			    (unsigned char)(r >> 8);
		}
		/* The forms past U+10FFFF - 0xf5..0xfd first, 0xf4 0x90.. - are
		 * glibc's and not RFC 3629's: those rounds only check that
		 * this decoder says EILSEQ somewhere. */
		int beyond = 0;
		for (i = 0; i < n; i++)
			if (buf[i] >= 0xf5 || (buf[i] == 0xf4 && i + 1 < n &&
			    buf[i + 1] >= 0x90))
				beyond = 1;
		memset(&sa, 0, sizeof(sa));
		memset(&sb, 0, sizeof(sb));
		if (beyond) {
			for (off = 0; off < n; ) {
				rb = my_mbrtoc32(&cb, (char *)buf + off, n - off, &sb);
				if (rb == (size_t)-1 || rb == (size_t)-2)
					break;
				off += rb ? rb : 1;
			}
			check(rb == (size_t)-1 || rb == (size_t)-2, "beyond",
			    buf[0]);
			continue;
		}
		for (off = 0; off < n; ) {
			size_t chunk = 1 + (unsigned)(rand_r(&seed) % (n - off));

			if (chunk > n - off)
				chunk = n - off;
			ca = cb = 0x55;
			ra = my_mbrtoc32(&ca, (char *)buf + off, chunk, &sa);
			rb = mbrtoc32(&cb, (char *)buf + off, chunk, &sb);
			if (ra == (size_t)-1 && rb == (size_t)-2) {
				/* An overlong form or a surrogate: told here by
				 * its second byte, by glibc when it is whole.
				 * glibc must refuse it too, given the rest. */
				size_t at = off + chunk;

				while (rb == (size_t)-2 && at < n)
					rb = mbrtoc32(&cb, (char *)buf + at++, 1,
					    &sb);
				check(rb == (size_t)-1 || rb == (size_t)-2,
				    "refused early", buf[off]);
				break;
			}
			if (!(ra == rb && (ra > chunk || ca == cb)) && bad < 20) {
				printf("random:");
				for (i = 0; i < n; i++)
					printf(" %02x", buf[i]);
				printf(" at %zu+%zu: here %zd U+%04x, glibc %zd "
				    "U+%04x\n", off, chunk, (ssize_t)ra,
				    (unsigned)ca, (ssize_t)rb, (unsigned)cb);
			}
			check(ra == rb && (ra > chunk || ca == cb), "random",
			    (unsigned long)buf[off] << 8 | chunk);
			if (ra == (size_t)-1 || rb == (size_t)-1 || ra != rb)
				break;
			off += ra == (size_t)-2 ? chunk : ra ? ra : 1;
		}
	}
	printf("UCHARTEST %ld checks, %ld failed\n", checks, bad);
	return bad != 0;
}
