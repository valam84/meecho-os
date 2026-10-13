.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	eepromread
.endif # ${MACHINE_ARCH} == "earm"

SUBDIR+=	diff
SUBDIR+=	grep
SUBDIR+=	mined
SUBDIR+=	ministat
SUBDIR+=	mtop
SUBDIR+=	toproto
SUBDIR+=	trace

# The compositor (stage 11.4), on the Wayland stack in external/mit.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	meechowm
.endif

.include <bsd.subdir.mk>
