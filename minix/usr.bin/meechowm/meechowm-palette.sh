#!/bin/sh
#
# meechowm-palette - the launcher and the command palette of the desktop
# (stage 11.7), Super+d in meechowm (MEECHOWM_LAUNCHER): the compositor's
# own commands and the programs worth starting from it, in one list that
# fuzzel narrows as one types.  The choice is run; text that matches
# nothing is run as a command of its own, as dmenu_run would.
#
# An entry is "TEXT<tab>COMMAND": fuzzel shows the text and gives back the
# command (--with-nth=1, --accept-nth=2), or what was typed.  The commands
# are meechowm -m's (strip.c lists them), which reach the compositor that
# started this through the socket in XDG_RUNTIME_DIR.
#
# Its look - font, size, MEECHO's colours - is fuzzel's configuration,
# /etc/xdg/fuzzel/fuzzel.ini (minix/share/style), or the user's own.
#
# Environment: MEECHOWM_TERM (the terminal, foot), MEECHOWM_FONT (a
# fontconfig pattern, for another font than the configuration's).

T=${MEECHOWM_TERM:-foot}

entries() {
	printf '%s\t%s\n' \
	    "Terminal"				"meechowm -m terminal" \
	    "Files (nnn)"			"$T -e nnn -e" \
	    "System monitor (mtop)"		"$T -e mtop" \
	    "Editor (vi)"			"$T -e vi" \
	    "Screenshot (to ~/Pictures)"	"meechowm-shot" \
	    "Screenshot of an area (drag it)"	"meechowm-shot -r" \
	    "Width: a third"			"meechowm -m width 1/3" \
	    "Width: a half"			"meechowm -m width 1/2" \
	    "Width: two thirds"			"meechowm -m width 2/3" \
	    "Width: all of the screen, or back"	"meechowm -m maximize" \
	    "Centre the column"			"meechowm -m center" \
	    "Close the window"			"meechowm -m close" \
	    "Workspace above"			"meechowm -m focus up" \
	    "Workspace below"			"meechowm -m focus down" \
	    "Move the column a workspace up"	"meechowm -m move up" \
	    "Move the column a workspace down"	"meechowm -m move down" \
	    "Motion off"			"meechowm -m anim 0" \
	    "Motion on"				"meechowm -m anim 200" \
	    "Leave the desktop"			"meechowm -m quit" \
	    "Restart the machine"		"/sbin/reboot" \
	    "Turn the machine off"		"/sbin/poweroff"
	# The screen's modes, as the driver lists them: "1920x1080@60
	# current" - type "screen" and a size to narrow them down.
	meechowm -m modes 2>/dev/null | while read -r m now; do
		printf 'Screen: %s Hz%s\t%s\n' "$(echo "$m" | sed 's/@/ at /')" \
		    "${now:+ (now)}" "meechowm -m mode $m"
	done
}

cmd=$(entries | fuzzel --dmenu --with-nth=1 --accept-nth=2 \
    ${MEECHOWM_FONT:+--font="$MEECHOWM_FONT"}) || exit 0
[ -n "$cmd" ] || exit 0
exec /bin/sh -c "$cmd"
