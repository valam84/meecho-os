/*
 * libgbm's backend, linked in: MEECHO's replacement of src/gbm/main/
 * backend.c.
 *
 * Mesa's backend.c finds a backend by name - GBM_BACKEND, the DRM driver's
 * name, then "dri" - and loads <name>_gbm.so from /usr/lib/gbm with
 * dlopen().  A static program cannot load anything, so the one backend
 * there is, DRI over libgallium (src/gbm/backends/dri/gbm_dri.c), is built
 * into this library and asked directly - the same calls backend.c makes
 * once dlsym() has found it.  GBM_BACKEND, if set, must name it.
 */

#include <stdlib.h>
#include <string.h>

#include "util/macros.h"
#include "util/os_misc.h"

#include "backend.h"
#include "gbm_backend_abi.h"

/* gbm_dri.c: GBM_GET_BACKEND_PROC, which backend.c would dlsym(). */
struct gbm_backend *GBM_GET_BACKEND_PROC(const struct gbm_core *);

/* What backend.c keeps of a loaded backend; here there is nothing to unload. */
struct gbm_backend_desc {
	const struct gbm_backend *backend;
};

static struct gbm_backend_desc dri_desc;

struct gbm_device *
_gbm_create_device(int fd)
{
	const char *b = os_get_option("GBM_BACKEND");
	struct gbm_device *dev;
	uint32_t abi_ver;

	if (b != NULL && strcmp(b, "dri") != 0)
		return NULL;	/* nothing else is built in */
	if (dri_desc.backend == NULL)
		dri_desc.backend = GBM_GET_BACKEND_PROC(&gbm_core);
	if (dri_desc.backend == NULL)
		return NULL;

	abi_ver = MIN2(GBM_BACKEND_ABI_VERSION,
	    dri_desc.backend->v0.backend_version);
	dev = dri_desc.backend->v0.create_device(fd, abi_ver);
	if (dev != NULL) {
		if (abi_ver != dev->v0.backend_version) {
			dev->v0.destroy(dev);
			return NULL;
		}
		dev->v0.backend_desc = &dri_desc;
	}
	return dev;
}

void
_gbm_device_destroy(struct gbm_device *gbm)
{
	gbm->v0.destroy(gbm);
}
