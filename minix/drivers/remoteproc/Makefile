.include <bsd.own.mk>

# Processors of the SoC other than the application cores, and the shared
# memory and links to them (port/clover/hw-modem.md).
.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	qcom_mss
SUBDIR+=	qcom_rmtfs_mem
.endif # ${MACHINE_ARCH} == "aarch64"

.include <bsd.subdir.mk>
