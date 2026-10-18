/* Host stand: <minix/ds.h> as far as touch.c uses it. */
#ifndef _STUB_MINIX_DS_H
#define _STUB_MINIX_DS_H
#include <stdint.h>
#define DS_MAX_KEYLEN	80
#define DSF_OVERWRITE	0x01
#define DSF_INITIAL	0x20
int ds_publish_u32(const char *name, uint32_t val, int flags);
int ds_retrieve_u32(const char *name, uint32_t *val);
int ds_subscribe(const char *regex, int flags);
int ds_check(char *name, int *type, int *owner_e);
#endif
