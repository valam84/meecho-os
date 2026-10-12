/* Стенд sunxi: <minix/sysutil.h>. */
#ifndef _H_MINIX_SYSUTIL_H
#define _H_MINIX_SYSUTIL_H
#define EP_SET 1
void micro_delay(unsigned long micros);
unsigned long micros_to_ticks(unsigned long micros);
int env_parse(const char *name, const char *fmt, int field, long *param,
	long min, long max);
#endif
