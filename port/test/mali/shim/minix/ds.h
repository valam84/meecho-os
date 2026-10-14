/*
 * Стенд mali: <minix/ds.h> - ключи, которые служба читает в DS (буферы
 * развёртки fb, meecho_drm.h); отвечает модель (model_ds_set()).
 */
#ifndef _SHIM_MINIX_DS_H
#define _SHIM_MINIX_DS_H
#include <minix/type.h>
#define DS_MAX_KEYLEN	80
int ds_retrieve_u32(const char *ds_name, u32_t *value);
int ds_retrieve_label_endpt(const char *ds_name, endpoint_t *endpoint);
#endif
