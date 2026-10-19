.include <bsd.own.mk>

.if ${MKIMAGEONLY} == "no"
SUBDIR+=	lwip
SUBDIR+=	uds
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	qrtr
.endif
.endif # ${MKIMAGEONLY} == "no"

.include <bsd.subdir.mk>
