# Makefile for the mmc drivers.
#
# Three drivers live here and a machine builds one or two of them.  What
# they share is sdhcreg.h and sdmmcreg.h - the SD specification written down
# - which is why they are in one directory rather than three.
#
#   mmc    the TI OMAP MMCHS, on the BeagleBone and its relatives (arm)
#   emmc   the same silicon reached differently, for the eMMC there (arm)
#   sdmmc  every SD/MMC controller of the aarch64 port: the SD Host
#          Controller standard and the Synopsys DesignWare MSHC, picked by
#          the device tree; sdmmc.h says why it is new code rather than a
#          host under mmchost.h
.include <bsd.own.mk>

.if ${MACHINE_ARCH} == "aarch64"

PROGS=		sdmmc
MAN.sdmmc=
SRCS.sdmmc=	sdmmc.c sdmmc_card.c sdmmc_host.c sdmmc_sdhci.c \
		sdmmc_dwmshc.c sdmmc_sunxi.c sdmmc_clk.c sdmmc_clk_rk3568.c \
		sdmmc_clk_h616.c sdmmc_ring.c

.else

PROGS=      mmc emmc
MAN.mmc=
MAN.emmc=

SRCS.mmc=  mmcblk.c mmchost_dummy.c sdhcreg.h sdmmcreg.h
SRCS.mmc += mmchost_mmchs.c
SRCS.emmc= emmc.c mmcblk.c

.endif

DPADD+=	${LIBBLOCKDRIVER} ${LIBSYS}
LDADD+=	-lblockdriver -lsys
CLEANFILES+=.depend mmcblk.d

#
# This is a system driver.
CPPFLAGS+= -D_SYSTEM=1

.include <minix.service.mk>
