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

/*
 * The same in a chosen width. Sixteen digits is right for an address and
 * wrong for a six-bit field: an exception class printed as
 * 0x0000000000000025 buries the two digits that carry the meaning.
 */
void kput_hexn(uint64_t value, unsigned digits);

/*
 * Decimal, for the things that are counts rather than addresses: a tick
 * number, a frequency in hertz, how many interrupt lines the controller has.
 * Reading those in hex is a needless translation.
 */
void kput_dec(uint64_t value);

#endif /* _AARCH64_KPRINT_H_ */
