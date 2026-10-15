#!/bin/sh
#
# meecho-desktop - the MEECHO desktop, from the console (stage 11.7):
# meechowm, and in it the wallpaper (wbg), the panel (meechowm-bar), the
# cards for restarted services (meechowm-notify) and a terminal (foot).
# Super+d is the palette, Print a screenshot; Super+Shift+e ends it, and
# the console has the screen back.
#
# Everything on it wears MEECHO's style (minix/share/style): the colours of
# /usr/share/meecho/palette, made into foot's and fuzzel's configuration
# in /etc/xdg, the panel's in /usr/share/meecho/bar.yml, the wallpaper and
# meechowm's focus ring; the typeface is JetBrains Mono (fontconfig's monospace).
#
# Set up here for what runs on the desktop: XDG_RUNTIME_DIR (the user's
# own directory, made if need be - Wayland's socket and the compositor's
# live there), and the file manager's colours as ANSI ones, so that nnn
# takes them from foot's palette, which is MEECHO's.
#
# Environment: MEECHOWM_WALLPAPER (another picture, PNG or SVG, or none),
# MEECHOWM_TERM (the terminal) and the rest of meechowm's (meechowm.c).

if [ -n "$WAYLAND_DISPLAY" ]; then
	echo "meecho-desktop: already on a desktop ($WAYLAND_DISPLAY)" >&2
	exit 1
fi

: ${XDG_RUNTIME_DIR:=/tmp/xdg-$(id -u)}
mkdir -p "$XDG_RUNTIME_DIR" && chmod 700 "$XDG_RUNTIME_DIR" || exit 1
export XDG_RUNTIME_DIR

# nnn: the context colour, then block and character devices, directories,
# executables, regular files, hard and symbolic links, missing, orphans,
# FIFOs, sockets and the rest - all of them among the first sixteen.
: ${NNN_COLORS:=4444}
: ${NNN_FCOLORS:=030304020006060801050508}
export NNN_COLORS NNN_FCOLORS

wall=${MEECHOWM_WALLPAPER:-/usr/share/meecho/wallpaper.svg}
startup="meechowm-bar & meechowm-notify & ${MEECHOWM_TERM:-foot}"
[ "$wall" = none ] || startup="wbg '$wall' & $startup"

exec meechowm -s "$startup"
