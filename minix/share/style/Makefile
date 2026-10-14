# MEECHO's house style (stage 11.7): its colours, its typeface and its
# wallpaper, and the configuration of the desktop's programs made from
# them, so that the terminal, the panel, the launcher, the compositor and
# the screen console look like one system.
#
#   palette		the colours - the one place they are written
#   style.awk		the palette as filled templates and as a C header
#   *.in		templates: @NAME@ is the palette's colour NAME
#   56-meecho.conf	fontconfig: Go Mono is monospace, Go the rest
#			(the fonts: external/bsd/gofont)
#
# Installed: /etc/xdg/foot/foot.ini and /etc/xdg/fuzzel/fuzzel.ini (the
# system's configuration, which a user's in ~/.config replaces),
# /usr/share/meecho/{bar.yml,wallpaper.svg,palette} (meechowm-bar and
# meecho-desktop take them from there), /usr/share/meecho/slurp.sh
# (slurp's arguments, which meechowm-shot -r gives it: slurp reads no
# configuration), the fontconfig file.  meechowm and the frame buffer
# driver's console make style.h from the palette with style.awk
# themselves (out=c).

.include <bsd.own.mk>

PALETTE=	${.CURDIR}/palette
STYLEAWK=	${.CURDIR}/style.awk

STYLED=		foot.ini fuzzel.ini bar.yml wallpaper.svg slurp.sh

.for _f in ${STYLED}
${_f}: ${_f}.in ${PALETTE} ${STYLEAWK}
	${_MKTARGET_CREATE}
	${TOOL_AWK} -v out=fill -f ${STYLEAWK} ${PALETTE} \
	    ${.CURDIR}/${_f}.in > ${.TARGET}.tmp && \
	    mv ${.TARGET}.tmp ${.TARGET}
FILES+=			${_f}
FILESBUILD_${_f}=	yes
.endfor
CLEANFILES+=	${STYLED} ${STYLED:=.tmp}

FILESDIR_foot.ini=	/etc/xdg/foot
FILESDIR_fuzzel.ini=	/etc/xdg/fuzzel
FILESDIR_bar.yml=	/usr/share/meecho
FILESDIR_wallpaper.svg=	/usr/share/meecho
FILESDIR_slurp.sh=	/usr/share/meecho

FILES+=			palette
FILESDIR_palette=	/usr/share/meecho

FILES+=			56-meecho.conf
FILESDIR_56-meecho.conf= /etc/fonts/conf.avail
SYMLINKS+=		../conf.avail/56-meecho.conf \
			/etc/fonts/conf.d/56-meecho.conf

.include <bsd.prog.mk>
