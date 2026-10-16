.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=	pckbd
.endif # ${MACHINE_ARCH} == "i386"

.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	qcom_touch
.endif # ${MACHINE_ARCH} == "aarch64"

.include <bsd.subdir.mk>
