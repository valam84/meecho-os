#!/bin/sh
#
# meechowm-bar - the panel of the desktop (stage 11.7, in MEECHO's style):
# yambar along the top of the screen, with MEECHO's configuration,
# /usr/share/meecho/bar.yml (made from the palette, minix/share/style) -
# unless the user has one of their own ($XDG_CONFIG_HOME/yambar/config.yml).
# meechowm lays its columns out below it (its exclusive zone, layers.c).
#
#   meechowm-bar            the panel
#   meechowm-bar status [MS] for yambar's script module: what the compositor
#                           says (meechowm -m state), as tag|type|value
#                           lines and an empty one - the workspaces, a dot
#                           each and the one on the screen filled, and the
#                           focused window's title
#   meechowm-bar load [MS]   the same for how busy the processors have been
#                           since the last time (kern.cp_time, kept in
#                           XDG_RUNTIME_DIR) and the memory in use, in MiB
#                           (/proc/meminfo: what is neither free nor cache)
#
# With MS each stays and writes a new set every MS milliseconds when it
# differs (yambar without a poll interval); without, once.
#
# yambar's threads (a module each, the bar's) wait in libgfxcompat's
# shared poll(2), which lets the others run: libmthread's threads switch
# only in its own calls.

# Both are meechowm -b (barstat.c): once here, for a person or a stand;
# the panel runs them with an interval, as processes that stay.
if [ "$1" = status ] || [ "$1" = load ]; then
	exec meechowm -b "$1" "${2:-0}"
fi

cfg=${XDG_CONFIG_HOME:-$HOME/.config}/yambar/config.yml
[ -f "$cfg" ] || cfg=/usr/share/meecho/bar.yml
exec yambar -c "$cfg"
