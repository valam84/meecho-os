# Inter 4.1 (stage 11.7): the MEECHO desktop's proportional face -
# sans-serif, serif and system-ui (minix/share/style, 56-meecho.conf).
# Made by Rasmus Andersson; SIL OFL 1.1 (dist/LICENSE.txt).  The import in
# dist/ is unchanged: extras/ttf of the release archive Inter-4.1.zip
# (port/gfx/import-stack.sh).
#
# Why this one: the Go it replaced read poorly on the board's screen.
# Covers Latin, Greek and Cyrillic.  Four static faces; the variable font,
# Inter Display and the other weights stay out.

.include <bsd.own.mk>

.PATH:		${.CURDIR}/dist

FILESDIR=	/usr/share/fonts/inter
FILES=		Inter-Regular.ttf Inter-Bold.ttf Inter-Italic.ttf \
		Inter-BoldItalic.ttf

.include <bsd.prog.mk>
