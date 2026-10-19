# Makefile for qcom_cpufreq: the Mi Pad 4's CPU clusters (Qualcomm SDM660)
# at frequencies their rails already hold - path A of port/clover/hw-cpu.md
# - fixed (stage=set) or by load (stage=gov, cf_gov.c).  The tablet ran it (hw-cpu.md, steps 3 and 4), and
# since step 5 the Mi Pad 4's rc starts it at every boot (rc.clover).
PROG=	qcom_cpufreq
SRCS=	cpufreq.c cf_find.c cf_volt.c cf_rail.c cf_clk.c cf_run.c cf_gov.c \
	spmi.c

FILES=${PROG}.conf
FILESNAME=${PROG}
FILESDIR= /etc/system.conf.d

# The SPMI arbiter's client is qcom_pmic's, as it is; only spmi_read() is
# called - the observer channels.
.PATH:	${.CURDIR}/../qcom_pmic
CPPFLAGS+=	-I${.CURDIR}/../qcom_pmic

DPADD+=	${LIBSYS} ${LIBTIMERS}
LDADD+=	-lsys -ltimers

WARNS?=	5

.include <minix.service.mk>
