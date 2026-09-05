.include <bsd.own.mk>

SUBDIR+=	btrace
SUBDIR+=	diskctl
SUBDIR+=	fbdctl
SUBDIR+=	mkfs.mfs
SUBDIR+=	mkproto

# Only the AArch64 kernel is handed its boot images as one archive, and the
# format header is only installed there.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	mkbootarchive
.endif

.include <bsd.subdir.mk>
