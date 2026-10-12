/* Стенд sunxi: то из <minix/drivers.h>, чем пользуются файлы драйвера. */
#ifndef _H_MINIX_DRIVERS_H
#define _H_MINIX_DRIVERS_H
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <minix/type.h>
#define OK		0
#define SELF		((endpoint_t)0x8ace)
#define ANY		((endpoint_t)0x7ace)
#define CLOCK		(-3)
#define HARDWARE	(-2)
#define UNUSED(x)	x __attribute__((unused))
#ifndef ENOTSUP
#define ENOTSUP		134
#endif
size_t strlcpy(char *dst, const char *src, size_t size);
#endif
