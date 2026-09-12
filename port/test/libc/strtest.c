/*
 * Строковые функции libc против побайтового эталона.
 *
 * Зачем: на aarch64 libc берёт memcpy, memmove, memset, memcmp, strlen,
 * strcmp и прочее из ассемблера lib/libc/arch/aarch64/string, снятого с
 * NetBSD 2015 года, когда этот порт NetBSD был новым. В ядре тот же слой
 * (libminc) уже дал четыре сломанные функции при первой загрузке
 * (PORTING-LOG, «Родовое ядро заговорило»). Ошибка в такой функции не
 * падает: она портит данные в зависимости от длины и выравнивания и
 * всплывает где-нибудь далеко - например, как base64, который на одном
 * ключе RSA декодирует не то, что хост.
 *
 * Как: для длин 0..MAXLEN и всех сочетаний смещений источника и приёмника
 * внутри строки кэша вызвать функцию и сравнить с циклом по байтам. Для
 * memmove - оба направления перекрытия. Собирается и хостовым gcc (там всё
 * должно пройти - это проверка самого стенда), и кросс-компилятором для
 * машины:
 *
 *	gcc -O2 -o strtest strtest.c && ./strtest
 *	board-cc.sh strtest.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLEN	520
#define ALIGN	32
#define AREA	(MAXLEN + 3 * ALIGN)

static unsigned char a[AREA], b[AREA], ref[AREA];
static unsigned long fails, checks;

static void
fill(unsigned char *p, size_t n, unsigned seed)
{
	size_t i;

	for (i = 0; i < n; i++)
		p[i] = (unsigned char)(seed * 31 + i * 7 + (i >> 3));
}

static void
fail(const char *what, size_t len, size_t so, size_t doff)
{
	if (fails < 40)
		printf("FAIL %-8s len %3zu src+%2zu dst+%2zu\n", what, len,
		    so, doff);
	fails++;
}

static int
ref_memcmp(const unsigned char *p, const unsigned char *q, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		if (p[i] != q[i])
			return p[i] < q[i] ? -1 : 1;
	return 0;
}

/* Сравнение своим циклом: проверяемым memcmp сверять проверяемое нельзя. */
static int
same(const unsigned char *p, const unsigned char *q)
{
	return ref_memcmp(p, q, AREA) == 0;
}

static void
test_memcpy(size_t len, size_t so, size_t doff)
{
	size_t i;

	fill(a, AREA, 1);
	fill(b, AREA, 2);
	fill(ref, AREA, 2);
	for (i = 0; i < len; i++)
		ref[doff + i] = a[so + i];
	memcpy(b + doff, a + so, len);
	checks++;
	if (!same(b, ref))
		fail("memcpy", len, so, doff);
}

static void
test_memmove(size_t len, size_t so, size_t doff)
{
	size_t i;

	/* Один буфер, перекрытие в обе стороны. */
	fill(a, AREA, 3);
	fill(ref, AREA, 3);
	for (i = 0; i < len; i++)
		ref[doff + i] = a[so + i];		/* a ещё не тронут */
	memmove(a + doff, a + so, len);
	checks++;
	if (!same(a, ref))
		fail("memmove", len, so, doff);
}

static void
test_memset(size_t len, size_t doff)
{
	size_t i;

	fill(b, AREA, 4);
	fill(ref, AREA, 4);
	for (i = 0; i < len; i++)
		ref[doff + i] = 0xa5;
	memset(b + doff, 0xa5, len);
	checks++;
	if (!same(b, ref))
		fail("memset", len, 0, doff);
}

static int
sign(int v)
{
	return v < 0 ? -1 : v > 0 ? 1 : 0;
}

static void
test_memcmp(size_t len, size_t so, size_t doff)
{
	size_t k;

	fill(a, AREA, 5);
	fill(b, AREA, 5);
	/* Равные, потом отличие в каждой позиции по разу для коротких. */
	checks++;
	if (sign(memcmp(a + so, b + doff, len)) != 0 && so == doff)
		fail("memcmp=", len, so, doff);
	for (k = 0; k < len; k += (len > 64 ? 7 : 1)) {
		fill(b, AREA, 5);
		b[doff + k] = (unsigned char)(a[so + k] + 1);
		checks++;
		if (sign(memcmp(a + so, b + doff, len)) !=
		    ref_memcmp(a + so, b + doff, len))
			fail("memcmp", len, so, doff);
	}
}

static void
test_str(size_t len, size_t so)
{
	size_t i;
	char *s = (char *)a + so, *t = (char *)b + so;

	for (i = 0; i < len; i++)
		s[i] = (char)('A' + (i * 5 + so) % 26);
	s[len] = '\0';
	s[len + 1] = 'x';				/* мусор за концом */
	checks++;
	if (strlen(s) != len)
		fail("strlen", len, so, 0);
	for (i = 0; i < len + 2; i++)
		t[i] = s[i];
	checks++;
	if (strcmp(s, t) != 0)
		fail("strcmp=", len, so, 0);
	if (len > 0) {
		t[len - 1] = (char)(t[len - 1] + 1);
		checks++;
		if (sign(strcmp(s, t)) != -1)
			fail("strcmp<", len, so, 0);
		checks++;
		if (strncmp(s, t, len - 1) != 0)
			fail("strncmp", len, so, 0);
	}
	memset(t, 0, AREA - so);
	strcpy(t, s);
	checks++;
	if (ref_memcmp((unsigned char *)s, (unsigned char *)t, len + 1) != 0)
		fail("strcpy", len, so, 0);
	checks++;
	if (strchr(s, '\0') != s + len)
		fail("strchr0", len, so, 0);
	if (len > 3) {
		s[len - 2] = '#';
		checks++;
		if (strchr(s, '#') != s + len - 2)
			fail("strchr", len, so, 0);
		checks++;
		if (memchr(s, '#', len) != s + len - 2)
			fail("memchr", len, so, 0);
	}
}

int
main(void)
{
	size_t len, so, doff;

	for (len = 0; len <= MAXLEN; len++) {
		for (so = 0; so < ALIGN; so++) {
			for (doff = 0; doff < ALIGN; doff++) {
				test_memcpy(len, so, doff);
				test_memmove(len, so, doff);
				if (len < 200 || so == doff)
					test_memcmp(len, so, doff);
			}
			test_memset(len, so);
			test_str(len, so);
		}
	}
	printf("%lu checks, %lu failures\n", checks, fails);
	return fails != 0;
}
