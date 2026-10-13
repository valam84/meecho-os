#!/bin/sh
#
# On the board, over ssh, with meechowm and foot running
# (board-meechowm.sh): text scrolling in foot as a load on pixman - every
# frame redraws the whole window, and the compositor composes it whole.
# What the cursor costs (board-cursorload.sh) is two small rectangles a
# frame and says little about pixman; this is the other end.
#
#   sh board-scroll.sh prep [LINES]    /tmp/big.txt (40000 lines, 4.4 MB)
#                                      and /tmp/scroll.sh
#   sh board-scroll.sh run TAG         types "sh /tmp/scroll.sh TAG" into
#                                      foot (usb_kbd inject=), waits for it
#
# scroll.sh, run by the shell in foot, cats the file to its terminal under
# time(1), with /proc/uptime and kern.cp_time before and after, into
# /tmp/scroll-TAG.out; "run" prints that and the md5 of the frame on
# display - the same file ends on the same screen, so the md5 must not
# depend on how pixman drew it (PIXMAN_DISABLE=arm-neon in the
# compositor's environment turns NEON off; pixman says so on stdout, which
# reaches /tmp/mwm.log only when the compositor exits).
#
# The keys go to foot alone: while a program reads /dev/kbdmux the input
# server gives the console none.  Nobody should be at the mouse or the
# keyboard meanwhile.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PATH

case "$1" in
prep)
	n=${2:-40000}
	awk -v n=$n 'BEGIN { for (i = 1; i <= n; i++)
	    printf "%06d The quick brown fox jumps over the lazy dog; " \
		"PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS 0123456789 {}[]<>|\n", i }' \
	    > /tmp/big.txt
	ls -l /tmp/big.txt
	cat > /tmp/scroll.sh <<'END'
#!/bin/sh
# In foot only: a stray copy typed anywhere else does nothing.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
case `tty` in /dev/pts/*) ;; *) exit 0;; esac
o=/tmp/scroll-$1.out
{ echo "PIXMAN_DISABLE=$PIXMAN_DISABLE"; cat /proc/uptime; sysctl kern.cp_time; } > $o
/usr/bin/time cat /tmp/big.txt 2>> $o
{ cat /proc/uptime; sysctl kern.cp_time; } >> $o
sleep 1
echo done >> $o
END
	;;
run)
	tag=${2:-x}
	rm -f /tmp/scroll-$tag.out
	minix-service up /service/usb_kbd -label kscroll \
	    -args "inject=sh%20/tmp/scroll.sh%20$tag%0a injectwait=1" || exit 1
	i=0
	while [ $i -lt 600 ] && ! grep -q done /tmp/scroll-$tag.out 2>/dev/null
	do
		sleep 1
		i=$((i + 1))
	done
	minix-service down kscroll
	cat /tmp/scroll-$tag.out 2>/dev/null || echo "no output from foot"
	# Height and stride as fbart reads them from the driver.
	set -- `/usr/tests/gfx/fbart -w 1 -h 1 -s 1 | sed -n \
	    '1s/^FBART [0-9]*x\([0-9]*\) stride \([0-9]*\).*/\1 \2/p'`
	echo "== md5 of the frame on display (stride $2, $1 lines)"
	dd if=/dev/fb0 bs=$2 count=$1 2>/dev/null | md5
	;;
*)
	echo "usage: sh board-scroll.sh prep [LINES] | run TAG" >&2
	exit 2
	;;
esac
