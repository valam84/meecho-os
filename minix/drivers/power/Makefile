.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "i386"
SUBDIR+=	acpi
.endif # ${MACHINE_ARCH} == "i386"

.if ${MACHINE_ARCH} == "earm"
SUBDIR+=	tps65217
SUBDIR+=	tps65950
.endif # ${MACHINE_ARCH} == "earm"

.if ${MACHINE_ARCH} == "aarch64"
SUBDIR+=	qcom_cpufreq
SUBDIR+=	qcom_pmic
SUBDIR+=	qcom_rpm
.endif # ${MACHINE_ARCH} == "aarch64"

.include <bsd.subdir.mk>
