/*
 * usb_hub stand: <minix/sysutil.h> as far as the driver uses it.
 */
#ifndef _STUB_MINIX_SYSUTIL_H
#define _STUB_MINIX_SYSUTIL_H

#define UNUSED(v)	UNUSED_ ## v __attribute((unused))

void env_setargs(int argc, char **argv);
void panic(const char *fmt, ...) __attribute__((noreturn));

#endif
