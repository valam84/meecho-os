/* Host stand: what adreno_drm.c asks DS (the scanout buffers fb published). */
#ifndef _STUB_MINIX_DS_H
#define _STUB_MINIX_DS_H
#include <minix/type.h>

#define DS_MAX_KEYLEN 80	/* as <minix/ds.h> */

int ds_retrieve_u32(const char *key, u32_t *val);
int ds_retrieve_label_endpt(const char *label, endpoint_t *endpoint);

#endif
