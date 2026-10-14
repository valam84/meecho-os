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

# The compositor (stage 11.4), on the Wayland stack in external/mit, and
# its x-ray view of the system and its cards for restarted services (11.7).
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	meechowm meechowm-xray meechowm-notify
.endif

.include <bsd.subdir.mk>
