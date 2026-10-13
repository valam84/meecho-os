#!/bin/sh
#
# meechowm-bar - the panel of the desktop (stage 11.7): yambar along the
# top of the screen, with the configuration MEECHO gives it - unless the
# user has one of their own ($XDG_CONFIG_HOME/yambar/config.yml).  meechowm
# lays its columns out below it (its exclusive zone, layers.c).
#
#   meechowm-bar            the panel
#   meechowm-bar status     for yambar's script module: what the compositor
#                           says (meechowm -m state), as tag|type|value
#                           lines and an empty one - the workspace on the
#                           screen of how many, and the focused window
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
		printf "workspace|string|%s/%s\n", a, n
		printf "title|string|%s\n\n", t
	}'
	exit 0
fi

cfg=${XDG_CONFIG_HOME:-$HOME/.config}/yambar/config.yml
if [ ! -f "$cfg" ]; then
	cfg=${XDG_RUNTIME_DIR:-/tmp}/meechowm-bar.yml
	cat > "$cfg" <<'EOF'
# The panel of the MEECHO desktop, as meechowm-bar writes it.
bar:
  location: top
  height: 24
  spacing: 8
  margin: 10
  font: monospace:size=11
  foreground: dfe6f0ff
  background: 1d2330ff
  left:
    - label:
        content:
          string: {text: "MEECHO", foreground: 7fc8ffff}
    - script:
        path: /usr/bin/meechowm-bar
        args: [status]
        poll-interval: 1000
        content:
          list:
            spacing: 12
            items:
              - string: {text: "{workspace}"}
              - string: {text: "{title}", max: 64}
  right:
    - clock:
        time-format: "%H:%M"
        date-format: "%a %d %b"
        content:
          string: {text: "{date}  {time}"}
EOF
fi
exec yambar -c "$cfg"
