# JetBrains Mono 2.304 (stage 11.7): the MEECHO desktop's monospace - the
# terminal, the panel, the launcher (minix/share/style, 56-meecho.conf).
# Made by JetBrains; SIL OFL 1.1 (dist/OFL.txt).  The import in dist/ is
# unchanged: fonts/ttf of the release archive JetBrainsMono-2.304.zip
# (port/gfx/import-stack.sh).
#
# Why this one: the Go Mono it replaced read poorly on the board's screen.
# Covers Latin, Greek and Cyrillic.  The ligature faces, not "NL": foot
# draws cell by cell and shapes nothing, so in the terminal the two are
# the same; the other weights and the variable font stay out.

.include <bsd.own.mk>

.PATH:		${.CURDIR}/dist

FILESDIR=	/usr/share/fonts/jetbrains-mono
FILES=		JetBrainsMono-Regular.ttf JetBrainsMono-Bold.ttf \
		JetBrainsMono-Italic.ttf JetBrainsMono-BoldItalic.ttf

.include <bsd.prog.mk>
