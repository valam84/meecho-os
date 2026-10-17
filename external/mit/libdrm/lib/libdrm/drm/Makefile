# The kernel interface headers, where libdrm's users look for them
# (<drm_fourcc.h> with -I/usr/include/libdrm).
#
# panfrost_drm.h is not libdrm's: it is the kernel's interface of Panfrost
# (Linux 7.0, include/uapi/drm, MIT), untouched, beside this file - what
# the GPU service mali serves (stage 11.6) and Mesa speaks.  msm_drm.h
# beside it is the same for msm - the kernel's header as Mesa 26.2.4
# carries it (include/drm-uapi, MIT), untouched: what the adreno service
# serves (port/clover/hw-gpu.md, step 7); libdrm's own copy is from 2015,
# without submit queues or sync objects, and this one is found first.

.include <bsd.init.mk>

.PATH:	${LIBDRMDIST}/include/drm

INCS=	drm.h \
	drm_fourcc.h \
	drm_mode.h \
	drm_sarea.h \
	amdgpu_drm.h \
	i915_drm.h \
	mach64_drm.h \
	mga_drm.h \
	msm_drm.h \
	nouveau_drm.h \
	panfrost_drm.h \
	qxl_drm.h \
	r128_drm.h \
	radeon_drm.h \
	savage_drm.h \
	sis_drm.h \
	tegra_drm.h \
	vc4_drm.h \
	via_drm.h \
	virtgpu_drm.h \
	vmwgfx_drm.h

INCSDIR=/usr/include/libdrm

.include <bsd.prog.mk>
