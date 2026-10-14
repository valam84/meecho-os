# Makefile for arch-dependent framebuffer code: the display of an AArch64
# machine, found in the device tree (see display.h for the parts).
.include <bsd.own.mk>

HERE=${.CURDIR}/arch/${MACHINE_ARCH}
.PATH:  ${HERE}

SRCS +=	fb_arch.c fb_rk.c rk_vop2.c dw_hdmi.c
CPPFLAGS += -I${HERE}
# The keys under which the scanout buffers are published in DS, for the
# GPU service to take them (stage 11.7): its header, meecho_drm.h.
CPPFLAGS.fb_arch.c += -iquote ${NETBSDSRCDIR}/minix/drivers/video/mali
