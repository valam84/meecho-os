#!/bin/sh
#
# meechowm-bar - the panel of the desktop (stage 11.7, in MEECHO's style):
# yambar along the top of the screen, with MEECHO's configuration,
# /usr/share/meecho/bar.yml (made from the palette, minix/share/style) -
# unless the user has one of their own ($XDG_CONFIG_HOME/yambar/config.yml).
# meechowm lays its columns out below it (its exclusive zone, layers.c).
#
#   meechowm-bar            the panel
#   meechowm-bar status     for yambar's script module: what the compositor
#                           says (meechowm -m state), as tag|type|value
#                           lines and an empty one - the workspaces, a dot
#                           each and the one on the screen filled, and the
#                           focused window's title
#   meechowm-bar load       the same for how busy the processors have been
#                           since the last time (kern.cp_time, kept in
#                           XDG_RUNTIME_DIR) and the memory in use, in MiB
#                           (/proc/meminfo: what is neither free nor cache)
#
# yambar's threads (a module each, the bar's) wait in libgfxcompat's
# shared poll(2), which lets the others run: libmthread's threads switch
# only in its own calls.

if [ "$1" = status ]; then
	meechowm -m state 2>/dev/null | awk '
	/^screen / {
		for (i = 1; i < NF; i++) {
			if ($i == "workspaces") n = $(i + 1)
			if ($i == "active") a = $(i + 1)
		}
	}
	/^column / && $9 == "focus" {
		split($2, wc, ".")
		if (wc[1] == a) {
			t = $0
			sub(/.* title /, "", t)
		}
	}
	END {
		# U+25CF and U+25CB, in UTF-8
		w = ""
		for (i = 1; i <= n; i++)
			w = w (i > 1 ? " " : "") \
			    (i == a ? "\342\227\217" : "\342\227\213")
		printf "workspace|string|%s\n", w
		printf "title|string|%s\n\n", t
	}'
	exit 0
fi

if [ "$1" = load ]; then
	last=${XDG_RUNTIME_DIR:-/tmp}/meechowm-bar.cpu
	# user nice sys intr idle, summed over the processors
	now=$(sysctl -n kern.cp_time 2>/dev/null | tr -cs '0-9' ' ')
	was=$(cat "$last" 2>/dev/null)
	echo "$now" > "$last"
	# page size, pages: total, free, largest free run, cache
	read -r ps total free largest cached < /proc/meminfo
	echo "$now $was $ps $total $free $cached" | awk '{
		cpu = 0
		if (NF == 14) {
			busy = $1 + $2 + $3 + $4; all = busy + $5
			busy0 = $6 + $7 + $8 + $9; all0 = busy0 + $10
			if (all > all0)
				cpu = int(100 * (busy - busy0) / (all - all0) + 0.5)
		}
		mem = int(($(NF - 2) - $(NF - 1) - $NF) * $(NF - 3) / 1048576)
		printf "cpu|int|%d\nmem|int|%d\n\n", cpu, mem
	}'
	exit 0
fi

cfg=${XDG_CONFIG_HOME:-$HOME/.config}/yambar/config.yml
[ -f "$cfg" ] || cfg=/usr/share/meecho/bar.yml
exec yambar -c "$cfg"
