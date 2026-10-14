#!/bin/sh
#
# On the board, over ssh: what the desktop costs when pixman draws its
# frames and when the GPU does (meechowm's MEECHOWM_RENDERER, stage 11.7).
# Starts meecho-desktop with the renderer named, opens a second foot, and
# measures the same loads each time:
#
#   idle       ten seconds of nothing
#   cursor     the cursor right and back, ROUNDS injector instances of
#              usb_mouse (32 reports each, one per tick), as
#              board-cursorload.sh moves it
#   overview   ten moves into the overview and out (Super+o), one a second
#   xray       ten seconds with the x-ray open (meechowm-xray), opened
#              before and closed after
#
# For each: the share of the machine busy (kern.cp_time: user, system;
# the four cores together), the compositor's own CPU time (ps), and what
# its frames cost by its own count (meechowm -m frames: drawing, waiting
# for the GPU, copying into the scanout buffer, flipping), and the CPU
# seconds of every process on the screen's path - the GPU and display
# services, VM, VFS, PM, the clients.  Then it ends
# the desktop, and the console has the screen back.
#
#   sh board-glesbench.sh pixman|gles2 [ROUNDS]     ROUNDS default 15
#
# Expects the mali service running for gles2 (minix-service up
# /service/mali), and what board-desk.sh expects.
PATH=/bin:/sbin:/usr/bin:/usr/sbin
HOME=${HOME:-/root}
XDG_RUNTIME_DIR=/tmp/xdg-0
export PATH HOME XDG_RUNTIME_DIR

R=${1:?pixman or gles2}
ROUNDS=${2:-15}

# kern.cp_time as "user nice system interrupt idle", summed over the cores.
cp() {
	sysctl kern.cp_time | tr -c '0-9\n' ' ' | awk '{
	    n = NF; print $(n-4), $(n-3), $(n-2), $(n-1), $n }'
}

# The compositor's CPU time in seconds (ps gives [h:]mm:ss.ss or mm:ss).
mwm_time() {
	ps axo time,comm | awk '$2 ~ /meechowm$/ && $2 !~ /xray/ {
	    n = split($1, p, ":"); s = 0
	    for (i = 1; i <= n; i++) s = s * 60 + p[i]
	    print s; exit }'
}

# CPU seconds of the processes on the screen's path, one "name seconds"
# per line: the compositor, the GPU and display services, the memory and
# file servers, the clients.
path_times() {
	ps axo time,comm | awk '{
	    c = $2; sub(".*/", "", c)
	    if (c !~ /^(meechowm|meechowm-xray|mali|fb|vm|vfs|pm|input|foot|yambar|wbg)$/)
	        next
	    n = split($1, p, ":"); s = 0
	    for (i = 1; i <= n; i++) s = s * 60 + p[i]
	    t[c] += s }
	    END { for (c in t) print c, t[c] }'
}

measure() {
	name=$1
	shift
	meechowm -m frames reset > /dev/null
	path_times > /tmp/glesbench.p0
	c0=`cp`
	t0=`mwm_time`
	"$@"
	c1=`cp`
	t1=`mwm_time`
	path_times > /tmp/glesbench.p1
	echo "$c0 $c1" | awk -v name="$name" -v t0="$t0" -v t1="$t1" '{
	    u = $6 - $1; ni = $7 - $2; s = $8 - $3; it = $9 - $4; id = $10 - $5
	    tot = u + ni + s + it + id
	    if (tot == 0) tot = 1
	    printf "%-9s busy %5.1f %%  user %5.1f %%  sys %5.1f %%  " \
	        "meechowm %.2f s\n", name, 100 * (tot - id) / tot,
	        100 * u / tot, 100 * (s + it) / tot, t1 - t0 }'
	echo "          `meechowm -m frames`"
	# What each process on the path spent, a client gone since is left out.
	awk 'NR == FNR { a[$1] = $2; next }
	    { d = $2 - a[$1]; if (d >= 0.01) line = line sprintf(" %s %.2f", $1, d) }
	    END { print "          cpu s:" line }' /tmp/glesbench.p0 \
	    /tmp/glesbench.p1
}

load_idle() {
	sleep 10
}

load_cursor() {
	right=""
	left=""
	i=0
	while [ $i -lt 32 ]; do
		right="$right${right:+,}0:6:0"
		left="$left${left:+,}0:-6:0"
		i=$((i + 1))
	done
	n=0
	while [ $n -lt $ROUNDS ]; do
		if [ $((n % 2)) -eq 0 ]; then dir=$right; else dir=$left; fi
		minix-service up /service/usb_mouse -label gload$n \
		    -args "inject=$dir injectwait=1" 2>/dev/null
		sleep 2
		minix-service down gload$n 2>/dev/null
		n=$((n + 1))
	done
}

load_overview() {
	n=0
	while [ $n -lt 10 ]; do
		meechowm -m overview > /dev/null
		sleep 1
		n=$((n + 1))
	done
}

# Opened before the measure (its start is not the view), closed after it:
# the client is in both samples of path_times.
load_xray() {
	sleep 10
}

trap '' HUP
MEECHOWM_RENDERER=$R meecho-desktop < /dev/null > /tmp/desk-$R.log 2>&1 &
echo $! > /tmp/desk.pid
n=0
while [ $n -lt 60 ] && ! meechowm -m state 2>/dev/null |
    grep -q '^column 1\.1 .* title [^-]'; do
	sleep 2
	n=$((n + 1))
done
meechowm -m terminal > /dev/null
sleep 20	# the second foot, the panel and the wallpaper settle
echo "== $R: `meechowm -m frames | cut -d' ' -f1-2`, `meechowm -m state |
    grep -c '^column'` window(s)"
grep -E 'frames drawn by|no GPU|GLES2|EGL|GBM|dma-buf' /tmp/desk-$R.log |
    head -5
measure idle load_idle
measure cursor load_cursor
measure overview load_overview
meechowm -m xray > /dev/null
sleep 3
measure xray load_xray
meechowm -m xray > /dev/null
sleep 1
meechowm -m quit > /dev/null 2>&1 || kill `cat /tmp/desk.pid`
sleep 3
echo "== $R: done"
