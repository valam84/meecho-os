# fontconfig's configuration in NetBSD's layout: fonts.conf in /etc/fonts,
# every template in /etc/fonts/conf.avail, and in /etc/fonts/conf.d links
# to the ones meson enables by default (conf.d/meson.build: hinting
# "slight", no sub-pixel rendering, no bitmaps except emoji) - the set the
# prototype ran with.  fonts.conf and 35-lang-normalize.conf are generated
# (../include): the font directory /usr/share/fonts, the cache in
# /var/cache/fontconfig.

.include <bsd.init.mk>

.PATH:	${FONTCONFIGINCLUDE} ${FONTCONFIGDIST} ${FONTCONFIGDIST}/conf.d

FILESDIR=	/etc/fonts
FILES=		fonts.conf fonts.dtd

CONF_AVAIL= \
	05-reset-dirs-sample.conf \
	09-autohint-if-no-hinting.conf \
	10-autohint.conf \
	10-hinting-full.conf \
	10-hinting-medium.conf \
	10-hinting-none.conf \
	10-hinting-slight.conf \
	10-no-antialias.conf \
	10-scale-bitmap-fonts.conf \
	10-sub-pixel-bgr.conf \
	10-sub-pixel-none.conf \
	10-sub-pixel-rgb.conf \
	10-sub-pixel-vbgr.conf \
	10-sub-pixel-vrgb.conf \
	10-unhinted.conf \
	10-yes-antialias.conf \
	11-lcdfilter-default.conf \
	11-lcdfilter-legacy.conf \
	11-lcdfilter-light.conf \
	11-lcdfilter-none.conf \
	20-unhint-small-vera.conf \
	25-unhint-nonlatin.conf \
	30-metric-aliases.conf \
	35-lang-normalize.conf \
	40-nonlatin.conf \
	45-generic.conf \
	45-latin.conf \
	48-guessfamily.conf \
	48-spacing.conf \
	49-sansserif.conf \
	50-user.conf \
	51-local.conf \
	60-generic.conf \
	60-latin.conf \
	65-fonts-persian.conf \
	65-khmer.conf \
	65-nonlatin.conf \
	69-unifont.conf \
	70-no-bitmaps.conf \
	70-no-bitmaps-and-emoji.conf \
	70-no-bitmaps-except-emoji.conf \
	70-yes-bitmaps.conf \
	80-delicious.conf \
	90-synthetic.conf

CONF_LINKS= \
	10-hinting-slight.conf \
	10-scale-bitmap-fonts.conf \
	10-sub-pixel-none.conf \
	10-yes-antialias.conf \
	11-lcdfilter-default.conf \
	20-unhint-small-vera.conf \
	30-metric-aliases.conf \
	40-nonlatin.conf \
	45-generic.conf \
	45-latin.conf \
	48-guessfamily.conf \
	48-spacing.conf \
	49-sansserif.conf \
	50-user.conf \
	51-local.conf \
	60-generic.conf \
	60-latin.conf \
	65-fonts-persian.conf \
	65-nonlatin.conf \
	69-unifont.conf \
	70-no-bitmaps-except-emoji.conf \
	80-delicious.conf \
	90-synthetic.conf

.for _f in ${CONF_AVAIL}
FILES+=			${_f}
FILESDIR_${_f}=		/etc/fonts/conf.avail
.endfor

.for _f in ${CONF_LINKS}
SYMLINKS+=	../conf.avail/${_f} /etc/fonts/conf.d/${_f}
.endfor

.include <bsd.prog.mk>
