/*
 * <uchar.h> (C11) for the gfx sysroot only.  The system's libc predates
 * it.  char32_t and char16_t are the C11 types, and char32_t is UTF-32.
 *
 * The multibyte side is UTF-8 whatever the locale, as musl has it.  It
 * would be the locale's encoding, but the static libc has single-byte
 * locales only (lib/libc/Makefile: multibyte locales are for libc.so), so
 * no static program can have a UTF-8 locale yet - and the desktop speaks
 * UTF-8: foot reads the terminal with mbrtoc32(), checks the locale with
 * it, and refuses to run when it does not decode UTF-8.  (compat/uchar.c)
 *
 * What stays the locale's: MB_CUR_MAX (1 in "C" - a buffer for one
 * character from c32rtomb() wants MB_LEN_MAX), and wcwidth(3), which
 * knows no width beyond ASCII in "C".
 */
#ifndef _GFX_UCHAR_H
#define _GFX_UCHAR_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifndef __cplusplus
typedef uint_least16_t char16_t;
typedef uint_least32_t char32_t;
#endif

size_t mbrtoc32(char32_t *, const char *, size_t, mbstate_t *);
size_t c32rtomb(char *, char32_t, mbstate_t *);
size_t mbrtoc16(char16_t *, const char *, size_t, mbstate_t *);
size_t c16rtomb(char *, char16_t, mbstate_t *);

#endif
