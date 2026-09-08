#!/bin/bash
# The stage 1 acceptance test: a static hello world, compiled and linked
# for aarch64-minix against the libc and csu installed in DESTDIR by the
# tree build. There is nothing to run it on yet; what is checked is that
# the compiler, the headers, crt0/crti/crtn/crtbegin/crtend, libc.a and
# libgcc.a all exist, agree with each other and link into one ELF.
set -euo pipefail
X=$HOME/xtools-aarch64/bin/aarch64-elf64-minix
DEST=$HOME/dest-evbarm64
W=$HOME/hello-aarch64
mkdir -p "$W"; cd "$W"
cat > hello.c <<'EOF'
#include <stdio.h>
#include <string.h>
#include <sys/utsname.h>
#include <minix/ipc.h>

/*
 * The message size is what stage 3 decided, and there is nothing here to run
 * the program on, so carry it out in a symbol the linker can be asked about
 * rather than in a printf nobody will see.
 */
_Static_assert(sizeof(message) == M_MESSAGE_SIZE, "message is not M_MESSAGE_SIZE bytes");
char msgsize_probe[sizeof(message)];

int
main(int argc, char **argv)
{
	message m;

	memset(&m, 0, sizeof(m));
	printf("hello from %s: %d args, message of %zu bytes, %s\n",
	    argv[0], argc, sizeof(m), "static");
	return 0;
}
EOF
$X-gcc -O2 -Wall -static -o hello hello.c
echo "--- link done"
file hello || true
$X-readelf -h hello | grep -E 'Class|Machine|Entry|Type'
$X-readelf -lW hello | grep -E 'LOAD|INTERP' || true
$X-nm hello | grep -E ' (main|__start|_ipc_sendrec_intr|printf|_brksize)$'
sz=$($X-nm -S hello | awk '$NF == "msgsize_probe" { print $2 }')
echo "sizeof(message) = $((16#$sz)) bytes"
$X-size hello
echo "--- disassembly of _ipc_sendrec_intr"
$X-objdump -d hello | sed -n '/<_ipc_sendrec_intr>:/,/ret/p'
