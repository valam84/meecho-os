.include <bsd.own.mk>

SUBDIR+=	btrace
SUBDIR+=	diskctl
SUBDIR+=	fbdctl
SUBDIR+=	mkfs.mfs
SUBDIR+=	mkproto
SUBDIR+=	sntp

# Only the AArch64 kernel is handed its boot images as one archive, and the
# format header is only installed there.  fbmode speaks the AArch64 frame
# buffer driver's own requests.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	fbmode
SUBDIR+=	mkbootarchive
.endif

.include <bsd.subdir.mk>
