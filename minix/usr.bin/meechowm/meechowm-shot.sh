#!/bin/sh
#
# meechowm-shot - a screenshot of the desktop (stage 11.7): Print in
# meechowm, "Screenshot" in its palette.  grim asks the compositor for the
# screen's pixels and writes them as a PNG - into ~/Pictures (made if need
# be; MEECHOWM_SHOTS for another directory), named by the time, or to the
# file given.  The name is printed.
#
# With -r, an area of the screen (Shift+Print, "Screenshot of an area"):
# slurp darkens the screen, a drag with the mouse marks the rectangle and
# its size is written beside it; grim takes what is inside (slurp prints
# the rectangle as grim's -g reads it).  Escape gives up, and nothing is
# written.  slurp's colours and typeface are MEECHO's, from
# /usr/share/meecho/slurp.sh (minix/share/style).
#
#   meechowm-shot [-r] [FILE]

area=
if [ "$1" = -r ]; then
	area=1
	shift
fi
dir=${MEECHOWM_SHOTS:-${HOME:-/tmp}/Pictures}
file=${1:-$dir/screen-$(date +%Y%m%d-%H%M%S).png}

if [ -n "$area" ]; then
	SLURP_STYLE=
	[ -r /usr/share/meecho/slurp.sh ] && . /usr/share/meecho/slurp.sh
	# slurp reads areas to offer from its input when that is not a
	# terminal: none are offered here.
	geometry=$(slurp -d $SLURP_STYLE </dev/null) || exit 1
	mkdir -p "$(dirname "$file")" && grim -g "$geometry" "$file" &&
	    echo "$file"
else
	mkdir -p "$(dirname "$file")" && grim "$file" && echo "$file"
fi
