# The Go fonts (stage 11.7): the typeface of the MEECHO desktop - Go Mono
# in the terminal, the panel and the launcher, Go for the rest
# (minix/share/style says which is which, through fontconfig).  Made by
# Bigelow & Holmes for the Go project; BSD-3 licence (dist/README).  The
# import in dist/ is unchanged: font/gofont/ttfs of golang.org/x/image at
# a998c07120b3 (2026-10-12).
#
# Why these: the tree carried no font at all - the desktop ran on the build
# host's DejaVu - and a font in the tree wants a licence like the tree's.
# Both families cover Latin, Greek and Cyrillic (WGL4).  Go Medium and Go
# Smallcaps stay in dist/: nothing on the desktop asks for them.

.include <bsd.own.mk>

.PATH:		${.CURDIR}/dist

FILESDIR=	/usr/share/fonts/go
FILES=		Go-Regular.ttf Go-Bold.ttf Go-Italic.ttf Go-Bold-Italic.ttf \
		Go-Mono.ttf Go-Mono-Bold.ttf Go-Mono-Italic.ttf \
		Go-Mono-Bold-Italic.ttf

.include <bsd.prog.mk>
