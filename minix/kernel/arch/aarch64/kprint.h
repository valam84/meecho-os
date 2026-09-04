/*
 * Minimal console output for early boot.
 *
 * There is no printf yet and this is not the place to write one: the real
 * kernel gets NetBSD's subr_prf once the generic kernel is linked in. Hex is
 * enough to report addresses and register values, which is all early boot has
 * to say.
 */

#ifndef _AARCH64_KPRINT_H_
#define _AARCH64_KPRINT_H_

#include <stdint.h>

void kputs(const char *s);
void kput_hex(uint64_t value);
void kput_line(const char *label, uint64_t value);

#endif /* _AARCH64_KPRINT_H_ */
