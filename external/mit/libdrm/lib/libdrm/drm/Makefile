# The kernel interface headers, where libdrm's users look for them
# (<drm_fourcc.h> with -I/usr/include/libdrm).

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
