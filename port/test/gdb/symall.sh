#!/bin/bash
# symall.sh binary addr...: function+offset for each address
B=/home/minix/xtools-aarch64/bin/aarch64-elf64-minix
bin=$1; shift
$B-nm -n "$bin" | grep -E " [Tt] " > /tmp/syms.$$
for a in "$@"; do
	awk -v a="$a" '{ v = strtonum("0x" $1) } v <= strtonum(a) { l = $3; b = v } END { printf "%s %s+%#x\n", a, l, strtonum(a) - b }' /tmp/syms.$$
done
rm -f /tmp/syms.$$
