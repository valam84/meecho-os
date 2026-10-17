/* usb_ecm stand: the directory service, which knows one label. */
#ifndef _STUB_MINIX_DS_H
#define _STUB_MINIX_DS_H
#include <minix/drivers.h>
int ds_retrieve_label_endpt(const char *label, endpoint_t *ep);
#endif
