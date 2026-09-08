# Проверка: даёт ли вендорский рецепт тот самый MAC 8a:6e:41:09:8e:2f
M = 0xffffffff


def rol32(v, n):
    return ((v << n) | (v >> (32 - n))) & M


def jhash(key, initval):
    length = len(key)
    a = b = c = (0xdeadbeef + length + initval) & M
    k = 0
    rem = length
    while rem > 12:
        a = (a + int.from_bytes(key[k:k + 4], 'little')) & M
        b = (b + int.from_bytes(key[k + 4:k + 8], 'little')) & M
        c = (c + int.from_bytes(key[k + 8:k + 12], 'little')) & M
        # __jhash_mix
        a = (a - c) & M; a ^= rol32(c, 4);  c = (c + b) & M
        b = (b - a) & M; b ^= rol32(a, 6);  a = (a + c) & M
        c = (c - b) & M; c ^= rol32(b, 8);  b = (b + a) & M
        a = (a - c) & M; a ^= rol32(c, 16); c = (c + b) & M
        b = (b - a) & M; b ^= rol32(a, 19); a = (a + c) & M
        c = (c - b) & M; c ^= rol32(b, 4);  b = (b + a) & M
        rem -= 12
        k += 12
    if rem >= 12: c = (c + (key[k + 11] << 24)) & M
    if rem >= 11: c = (c + (key[k + 10] << 16)) & M
    if rem >= 10: c = (c + (key[k + 9] << 8)) & M
    if rem >= 9:  c = (c + key[k + 8]) & M
    if rem >= 8:  b = (b + (key[k + 7] << 24)) & M
    if rem >= 7:  b = (b + (key[k + 6] << 16)) & M
    if rem >= 6:  b = (b + (key[k + 5] << 8)) & M
    if rem >= 5:  b = (b + key[k + 4]) & M
    if rem >= 4:  a = (a + (key[k + 3] << 24)) & M
    if rem >= 3:  a = (a + (key[k + 2] << 16)) & M
    if rem >= 2:  a = (a + (key[k + 1] << 8)) & M
    if rem >= 1:
        a = (a + key[k]) & M
        # __jhash_final
        c ^= b; c = (c - rol32(b, 14)) & M
        a ^= c; a = (a - rol32(c, 11)) & M
        b ^= a; b = (b - rol32(a, 25)) & M
        c ^= b; c = (c - rol32(b, 16)) & M
        a ^= c; a = (a - rol32(c, 4)) & M
        b ^= a; b = (b - rol32(a, 14)) & M
        c ^= b; c = (c - rol32(b, 24)) & M
    return c


# 16 байт по смещению 0x0a в OTP, снятые с платы
soc_id = bytes([0x4d, 0x34, 0x52, 0x30, 0x33, 0x39, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x15, 0x01])

h1 = jhash(soc_id, 0x35660001)
h2 = jhash(soc_id, 0x35660002)
addr = [h1 & 0xff, (h1 >> 8) & 0xff, (h1 >> 16) & 0xff, (h1 >> 24) & 0xff,
        h2 & 0xff, (h2 >> 8) & 0xff]
addr[0] = (addr[0] & 0xfe) | 0x02

got = ':'.join('%02x' % b for b in addr)
print('soc-id  ', soc_id.hex(' '))
print('h1 h2   %08x %08x' % (h1, h2))
print('получено', got)
print('на плате 8a:6e:41:09:8e:2f')
print('СОВПАЛО' if got == '8a:6e:41:09:8e:2f' else 'НЕ СОВПАЛО')
