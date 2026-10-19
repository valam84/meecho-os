#!/bin/sh
# Mutants of minix/drivers/net/wcn/wcn_ce.c itself: each takes away one
# rule of the cache handling (wcn_ce.h), and the stand must fail on it.
#   noline   - a receive descriptor written into a line that still holds
#              descriptors the engine owns
#   linux    - that, and the write index moved with every buffer, as
#              Linux does with its uncached rings
#   fromhw   - send room counted from the engine's read index (Linux)
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../../../minix/drivers/net/wcn
OUT=${OUT:-/tmp/wcn-stand}
mkdir -p "$OUT/mut"
CC=${CC:-cc}
CFLAGS="-O1 -g -Wall -Wextra -fsanitize=address,undefined -I$SRC"
fail=0

mutant() {
	name=$1; shift
	sed "$@" "$SRC/wcn_ce.c" > "$OUT/mut/wcn_ce-$name.c"
	if cmp -s "$SRC/wcn_ce.c" "$OUT/mut/wcn_ce-$name.c"; then
		echo "mutant $name: the edit did not apply"; fail=1; return
	fi
	$CC $CFLAGS -o "$OUT/mut/cetest-$name" "$HERE/cetest.c" \
	    "$OUT/mut/wcn_ce-$name.c"
	if "$OUT/mut/cetest-$name" > "$OUT/mut/$name.txt" 2>&1 &&
	    "$OUT/mut/cetest-$name" -s >> "$OUT/mut/$name.txt" 2>&1; then
		echo "MUTANT $name NOT CAUGHT"; fail=1
	else
		echo "mutant $name: caught - $(grep FAIL "$OUT/mut/$name.txt" |
		    head -n 1 | sed 's/.*: //')"
	fi
}

mutant noline -e 's/if (in_range(r->mask, (first + k) & r->mask, r->sw, r->pub))/if (0)/'
mutant linux -e 's/if (in_range(r->mask, (first + k) & r->mask, r->sw, r->pub))/if (0)/' \
    -e 's/to = r->wr & ~(unsigned)(DPL - 1) & r->mask;/to = r->wr;/'
mutant fromhw -e 's/return (r->sw - 1 - r->wr) \& r->mask;/return (src_hw(ce, \&ce->pipe[id]) - 1 - r->wr) \& r->mask;/'
exit $fail
