#!/bin/sh
# Scrolling in foot on the Mi Pad 4's desktop as a load on the frame path
# (the GPU draws the frames; are they copied into the scanout buffers or
# drawn there): foot is spawned on the running desktop with cat of a big
# file under time(1); around it kern.cp_time, /proc/uptime, the compositor's
# "frames" counters and ps times of fb, meechowm, adreno.  Run on the tablet
# as root, from ssh:
#
#	sh scrollbench.sh TAG [LINES]
PATH=/bin:/sbin:/usr/bin:/usr/sbin
export XDG_RUNTIME_DIR=/tmp/xdg-0
tag=${1:-x}
n=${2:-20000}
if [ ! -f /tmp/big-$n.txt ]; then
	awk -v n=$n 'BEGIN { for (i = 1; i <= n; i++)
	    printf "%06d The quick brown fox jumps over the lazy dog; " \
		"PACK MY BOX WITH FIVE DOZEN LIQUOR JUGS 0123456789 {}[]<>|\n", i }' \
	    > /tmp/big-$n.txt
fi
cat > /tmp/scrollrun.sh <<END
#!/bin/sh
o=/tmp/scroll-$tag.out
/usr/bin/time cat /tmp/big-$n.txt 2>> \$o
echo done >> \$o
END
o=/tmp/scroll-$tag.out
psx() { ps ax | grep -E ' (/service/fb|/service/adreno|meechowm -s)' | grep -v grep; }
{ echo "== $tag, $n lines"; psx; cat /proc/uptime; sysctl kern.cp_time; } > $o.pre
rm -f $o
meechowm -m frames reset >/dev/null
meechowm -m spawn foot sh /tmp/scrollrun.sh || exit 1
i=0
while [ $i -lt 600 ] && ! grep -q done $o 2>/dev/null; do
	sleep 1; i=$((i+1))
done
cat $o.pre
cat /proc/uptime; sysctl kern.cp_time; psx
meechowm -m frames
cat $o
