#ifndef _AARCH64_DIRECT_UTILS_H
#define _AARCH64_DIRECT_UTILS_H

#include "kernel/kernel.h"

/*
 * Output that does not go through a driver, a message or a buffer: what the
 * kernel uses before there is anything to talk to, and after a panic when
 * there is no longer anything to talk to. On this architecture it is the
 * console UART the board support package points it at.
 */
void direct_cls(void);
void direct_print(const char *);
void direct_print_char(char);
int direct_read_char(unsigned char *);

#endif /* _AARCH64_DIRECT_UTILS_H */
