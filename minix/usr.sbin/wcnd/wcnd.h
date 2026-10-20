#ifndef _WCND_H
#define _WCND_H

/* wcnd's own declarations, shared by wcnd.c and connect.c. */

#include <stddef.h>
#include <stdint.h>

#include "wcn_ioc.h"
#include "wcn_wmi.h"
#include "wcn_mlme.h"

extern int dev;				/* /dev/wlan0 */

void say(int pri, const char *fmt, ...);
int wmi_wait(struct wcn_msg *msg, unsigned ms);
int wmi_send(const uint8_t *p, size_t n, const char *what);

/* connect.c */
int join(const struct wcn_bss_table *t, const char *ssid, const char *pass,
	const struct wmi_svc_ready *svr, const uint8_t self[6]);
void keep(void);

#endif /* _WCND_H */
