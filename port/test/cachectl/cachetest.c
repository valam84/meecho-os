/*
 * Хостовой стенд для обслуживания кэшей по диапазону.
 *
 * Проверяется не то, что кэш согласован — этого на хосте не увидеть, — а
 * то, какие строки какую операцию получили. Именно это и есть та часть,
 * которую нельзя проверить нигде: QEMU кэши не моделирует вовсе, а на плате
 * ошибка в разбиении диапазона выглядит как редкая порча чужой памяти.
 *
 * Стенд включает kernel/arch/aarch64/cache.c целиком, подставив вместо трёх
 * инструкций регистратор. Правок в cache.c под стенд нет: он собран так,
 * что вне CACHE_TEST в нём остаётся только арифметика.
 *
 * Главное свойство, ради которого стенд написан (эталон — NetBSD,
 * _bus_dmamap_sync_segment в sys/arch/arm/arm32/bus_dma.c): при
 * CACHE_INVALIDATE строка, покрытая диапазоном не целиком, обязана быть
 * сначала записана и лишь затем сброшена. Иначе теряются чужие грязные
 * байты, лежащие в той же строке.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <minix/cachectl.h>

#define CACHE_TEST 1

/* --------------------------------------------------------------- регистратор */

enum { OP_CLEAN = 1, OP_INVAL, OP_FLUSH };

#define MAX_OPS 4096

static struct {
	unsigned long addr;
	int op;
} ops[MAX_OPS];

static int nops;
static int barriers;
static unsigned long test_line;

static void
record(unsigned long addr, int op)
{
	if (nops >= MAX_OPS) {
		fprintf(stderr, "too many operations\n");
		exit(1);
	}
	ops[nops].addr = addr;
	ops[nops].op = op;
	nops++;
}

#define DC_CLEAN(addr)		record((addr), OP_CLEAN)
#define DC_INVALIDATE(addr)	record((addr), OP_INVAL)
#define DC_FLUSH(addr)		record((addr), OP_FLUSH)
#define CACHE_BARRIER()		(barriers++)

static unsigned long
dcache_line_size(void)
{
	return test_line;
}

#include "cache.c"

/* ------------------------------------------------------------------ проверки */

static int failed;
static int checks;

static void
check(int ok, const char *what, unsigned long line, unsigned long start,
	unsigned long len)
{
	checks++;
	if (!ok) {
		failed++;
		printf("FAIL  line=%lu start=%lu len=%lu: %s\n",
		    line, start, len, what);
	}
}

static const char *
opname(int op)
{
	switch (op) {
	case OP_CLEAN:	return "clean";
	case OP_INVAL:	return "invalidate";
	case OP_FLUSH:	return "clean+invalidate";
	}
	return "?";
}

static void
dump(void)
{
	int i;

	for (i = 0; i < nops; i++)
		printf("        %#lx %s\n", ops[i].addr, opname(ops[i].op));
}

/*
 * Один прогон: позвать dcache_range() и проверить всё, что про его результат
 * известно заранее.
 */
static void
run(int op, unsigned long start, unsigned long len, unsigned long line)
{
	unsigned long end = start + len;
	unsigned long first = start & ~(line - 1);
	unsigned long last = len ? ((end - 1) & ~(line - 1)) : first;
	unsigned long a;
	int i;

	nops = 0;
	barriers = 0;
	test_line = line;

	dcache_range(op, start, len);

	if (len == 0) {
		check(nops == 0, "zero length did something", line, start, len);
		return;
	}

	check(barriers == 2, "not exactly two barriers", line, start, len);

	/* Каждая строка диапазона получила ровно одну операцию. */
	for (a = first; a <= last; a += line) {
		int seen = 0;

		for (i = 0; i < nops; i++)
			if (ops[i].addr == a)
				seen++;

		if (seen != 1) {
			check(0, "line not covered exactly once", line, start,
			    len);
			printf("        line %#lx seen %d times\n", a, seen);
			dump();
			return;
		}
	}

	/* И ни одна строка вне диапазона не тронута. */
	for (i = 0; i < nops; i++) {
		if (ops[i].addr < first || ops[i].addr > last) {
			check(0, "touched a line outside the range", line,
			    start, len);
			dump();
			return;
		}
		check((ops[i].addr & (line - 1)) == 0,
		    "operand not line-aligned", line, start, len);
	}

	/* Операции упорядочены по адресу: каждая строка ровно один раз. */
	for (i = 1; i < nops; i++)
		check(ops[i].addr > ops[i - 1].addr, "operations out of order",
		    line, start, len);

	/* И та самая операция для той самой строки. */
	for (i = 0; i < nops; i++) {
		unsigned long ls = ops[i].addr;
		unsigned long le = ls + line;
		int whole = (ls >= start && le <= end);

		switch (op) {
		case CACHE_CLEAN:
			check(ops[i].op == OP_CLEAN, "clean asked for something"
			    " else", line, start, len);
			break;
		case CACHE_CLEAN_INVALIDATE:
			check(ops[i].op == OP_FLUSH, "clean+invalidate asked"
			    " for something else", line, start, len);
			break;
		case CACHE_INVALIDATE:
			/*
			 * Целая строка сбрасывается, частичная — сначала
			 * записывается. Обратное и есть та ошибка, ради
			 * которой всё это написано.
			 */
			if (whole)
				check(ops[i].op == OP_INVAL,
				    "whole line not plainly invalidated",
				    line, start, len);
			else
				check(ops[i].op == OP_FLUSH,
				    "PARTIAL LINE DISCARDED WITHOUT WRITEBACK",
				    line, start, len);
			break;
		}
	}


}

int
main(void)
{
	static const unsigned long lines[] = { 32, 64, 128 };
	static const int opers[] = {
		CACHE_CLEAN, CACHE_INVALIDATE, CACHE_CLEAN_INVALIDATE
	};
	unsigned long base = 0x40200000UL;	/* как адрес в линейной карте */
	size_t li, oi;
	unsigned long off, len;

	/*
	 * Сплошной перебор: три размера строки, три операции, начало в
	 * пределах двух строк и длина до четырёх. Этого хватает, чтобы
	 * каждое сочетание "начало ровное/нет" и "конец ровный/нет"
	 * встретилось многократно, включая диапазон короче одной строки.
	 */
	for (li = 0; li < sizeof(lines) / sizeof(lines[0]); li++) {
		unsigned long line = lines[li];

		for (oi = 0; oi < sizeof(opers) / sizeof(opers[0]); oi++) {
			for (off = 0; off <= 2 * line; off++) {
				for (len = 0; len <= 4 * line; len++) {
					run(opers[oi], base + off, len, line);
				}
			}
		}
	}

	/* И несколько случаев, названных по имени - чтобы отказ читался. */
	printf("\nnamed cases, line size 64:\n");
	test_line = 64;

	nops = 0;
	dcache_range(CACHE_INVALIDATE, base + 8, 16);
	printf("  invalidate 16 bytes inside one line:\n");
	dump();
	check(nops == 1 && ops[0].op == OP_FLUSH,
	    "a range inside one line must be written back, not discarded",
	    64, base + 8, 16);

	nops = 0;
	dcache_range(CACHE_INVALIDATE, base, 256);
	printf("  invalidate four whole lines:\n");
	dump();
	check(nops == 4 && ops[0].op == OP_INVAL && ops[3].op == OP_INVAL,
	    "whole lines must be discarded outright", 64, base, 256);

	nops = 0;
	dcache_range(CACHE_INVALIDATE, base + 32, 192);
	printf("  invalidate with both ends inside a line:\n");
	dump();
	check(nops == 4 && ops[0].op == OP_FLUSH && ops[1].op == OP_INVAL &&
	    ops[2].op == OP_INVAL && ops[3].op == OP_FLUSH,
	    "ends must be written back, middle discarded", 64, base + 32, 192);

	printf("\n%d checks, %d failed\n", checks, failed);
	return failed != 0;
}
