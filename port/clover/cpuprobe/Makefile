# The Mi Pad 4's CPU-frequency probe (port/clover/hw-cpu.md, step 1): a
# service that reads, and only reads, what the boot chain leaves in the
# blocks that clock and power the CPU clusters.  Not part of the system's
# build: port/clover/cpu-probe.sh builds it here and copies it to the
# tablet, as gpu-probe.sh does with the adreno service.
PROG=	cpuprobe
SRCS=	cpuprobe.c spmi.c
MAN=

# The SPMI arbiter's client is qcom_pmic's, as it is: reads only, through
# the observer channels.
.PATH:	${NETBSDSRCDIR}/minix/drivers/power/qcom_pmic
CPPFLAGS+=	-I${NETBSDSRCDIR}/minix/drivers/power/qcom_pmic

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
