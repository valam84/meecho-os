/*
 * ldtest - long double (binary128 on AArch64) through libgcc's helpers.
 *
 * libc on aarch64 no longer brings its own copies of the binary128 helpers
 * (qp.c); the compiler's libgcc gives them.  This checks the arithmetic,
 * the conversions and libc's long double input and output, which are what
 * would break if a helper were missing or wrong.  Prints "LDTEST ok" or
 * the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void
expect_str(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want) != 0) {
		printf("FAIL %s: got %s, want %s\n", what, got, want);
		fails++;
	}
}

int
main(int argc, char **argv)
{
	volatile long double a, b, c, d;
	char buf[128];
	volatile int i = 7;
	volatile double dd;

	a = strtold("3.25", NULL);
	b = strtold("-1.5e3", NULL);
	c = a * b + (long double)i;		/* __multf3, __addtf3, __floatsitf */
	snprintf(buf, sizeof(buf), "%.6Lf", c);
	expect_str("a*b+7", buf, "-4868.000000");

	d = c / 3;				/* __divtf3 */
	snprintf(buf, sizeof(buf), "%.10Lg", d);
	expect_str("c/3", buf, "-1622.666667");

	dd = (double)d;				/* __trunctfdf2 */
	snprintf(buf, sizeof(buf), "%.6f", dd);
	expect_str("(double)c/3", buf, "-1622.666667");

	if (!(a < 4.0L && a > 3.0L && b < a))	/* __lttf2, __gttf2 */
		fails++, printf("FAIL comparisons\n");
	if ((long)(a * 4) != 13)		/* __fixtfdi */
		fails++, printf("FAIL (long)(a*4)\n");

	printf("LDTEST %s\n", fails ? "FAILED" : "ok");
	return fails != 0;
}
