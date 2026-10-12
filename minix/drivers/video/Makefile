.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	fb
SUBDIR+=	tda19988
.endif # ${MACHINE_ARCH} == "earm"

# The frame buffer driver has an arch part for AArch64 (VOP2 and HDMI, found
# in the device tree); the TDA19988 is a BeagleBone part and stays there.
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	fb
.endif # ${MACHINE_ARCH} == "aarch64"

.include <bsd.subdir.mk>
