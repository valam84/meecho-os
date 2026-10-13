#pragma once
#include <stdint.h>

/* 8-bit input, 16-bit output */
extern const uint16_t srgb_decode_8_to_16_table[256];
static inline uint16_t
srgb_decode_8_to_16(uint8_t v)
{
    return srgb_decode_8_to_16_table[v];
}

/* 8-bit input, 8-bit output */
extern const uint8_t srgb_decode_8_to_8_table[256];

static inline uint8_t
srgb_decode_8_to_8(uint8_t v)
{
    return srgb_decode_8_to_8_table[v];
}
