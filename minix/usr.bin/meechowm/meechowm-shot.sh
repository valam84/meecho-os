#!/bin/sh
#
# meechowm-shot - a screenshot of the desktop (stage 11.7): Print in
# meechowm, "Screenshot" in its palette.  grim asks the compositor for the
# screen's pixels and writes them as a PNG - into ~/Pictures (made if need
# be; MEECHOWM_SHOTS for another directory), named by the time, or to the
# file given.  The name is printed.
#
#   meechowm-shot [FILE]

dir=${MEECHOWM_SHOTS:-${HOME:-/tmp}/Pictures}
file=${1:-$dir/screen-$(date +%Y%m%d-%H%M%S).png}
mkdir -p "$(dirname "$file")" && grim "$file" && echo "$file"
