#
# style.awk - MEECHO's palette (./palette) as what a program can read.
#
#   awk -v out=fill -f style.awk palette TEMPLATE
#	the template with every @NAME@ replaced by the colour's RRGGBB
#   awk -v out=c -f style.awk palette
#	a C header: #define STYLE_NAME 0xRRGGBB for every colour, and
#	STYLE_ANSI, the sixteen ANSI colours as an initialiser
#
# A name the palette has not got is an error, and so is a value that is
# neither RRGGBB nor a name defined above it: a misspelt colour stops the
# build rather than leaving "@acent@" in a configuration file.
#

function fail(msg) {
	printf "style.awk: %s:%d: %s\n", FILENAME, FNR, msg > "/dev/stderr"
	failed = 1
	exit 1
}

function hex6(v) {
	return length(v) == 6 && v ~ /^[0-9a-f]+$/
}

# The palette: the first file.
FNR == NR {
	sub(/#.*/, "")
	if (NF == 0)
		next
	if (NF != 2)
		fail("want NAME VALUE")
	if ($1 !~ /^[a-z][a-z0-9_]*$/)
		fail("a name is [a-z][a-z0-9_]*: " $1)
	if ($1 in colour)
		fail($1 " twice")
	v = $2
	if (!hex6(v)) {
		if (!(v in colour))
			fail("neither RRGGBB nor a colour above: " v)
		v = colour[v]
	}
	colour[$1] = v
	order[n++] = $1
	next
}

# A template.
out == "fill" {
	line = $0
	result = ""
	while (match(line, /@[a-z][a-z0-9_]*@/)) {
		name = substr(line, RSTART + 1, RLENGTH - 2)
		if (!(name in colour))
			fail("no colour " name)
		result = result substr(line, 1, RSTART - 1) colour[name]
		line = substr(line, RSTART + RLENGTH)
	}
	print result line
}

END {
	if (failed)
		exit 1
	if (out == "c") {
		print "/* Made from minix/share/style/palette by style.awk. */"
		print "#ifndef _MEECHO_STYLE_H_"
		print "#define _MEECHO_STYLE_H_"
		for (i = 0; i < n; i++)
			printf "#define STYLE_%s\t0x%s\n", toupper(order[i]),
			    colour[order[i]]
		printf "#define STYLE_ANSI\t{"
		for (i = 0; i < 16; i++) {
			if (!(("ansi" i) in colour)) {
				printf "\nstyle.awk: no ansi%d\n", i > "/dev/stderr"
				exit 1
			}
			printf "%s0x%s", i ? ", " : " ", colour["ansi" i]
		}
		print " }"
		print "#endif"
	} else if (out != "fill") {
		print "style.awk: out=fill or out=c" > "/dev/stderr"
		exit 1
	}
}
