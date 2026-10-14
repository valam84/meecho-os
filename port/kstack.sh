#!/bin/bash
# Turn the kernel's own stack trace in a console log back into names.
#
#	kstack.sh [KERNEL_ELF] < console.log
#
# panic() prints "kernel on CPU n:" and the return addresses on the kernel's
# stack, innermost first; an exception in the kernel prints "pc ... lr ..."
# before it. This picks those lines out and runs each address through
# addr2line against the kernel that printed them - which has to be that very
# kernel: addresses mean nothing against another build. A return address is
# the instruction after the call, so the line asked about is the one four
# bytes before it, the call itself.
#
# See port/PORTING-LOG.md, "Стек ядра в панике".
K=${1:-$HOME/obj-evbarm64/minix/kernel/kernel}
A2L=${A2L:-$HOME/xtools-aarch64/bin/aarch64-elf64-minix-addr2line}

[ -f "$K" ] || { echo "no kernel $K" >&2; exit 1; }

tr -d '\r' | grep -a -E "kernel on CPU [0-9]+:|^pc 0x[0-9a-f]+ lr " |
while read -r line; do
	echo "$line"
	case "$line" in
	pc*)	back=0		# pc and lr are where they are; sp is no code
		addrs=$(echo "$line" | grep -oE "(pc|lr) 0x[0-9a-f]{12,}" |
		    cut -d' ' -f2) ;;
	*)	back=4		# return addresses: the call is before
		addrs=$(echo "${line#*:}" | grep -oE "0x[0-9a-f]{12,}") ;;
	esac
	for a in $addrs; do
		at=$(printf '0x%x' $((a - back)))
		printf '    %s  %s\n' "$a" "$("$A2L" -f -p -e "$K" "$at" |
		    sed 's| at .*/minix/kernel/| at |' | head -1)"
	done
done
