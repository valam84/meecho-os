#!/usr/bin/env python3
"""A frame read from /dev/fb0 (XRGB8888, rows STRIDE bytes apart), as a PNG.

The board's frame comes over ssh - "fbart -w 1 -h 1" says the geometry
("FBART 1280x720 stride 5120 ..."), dd copies H rows of STRIDE bytes -
and this writes it as an RGB PNG with zlib alone.  board-shot.sh does
both ends; qemu-shot.py has the same conversion for QEMU's console.

    fbpng.py frame.raw[.gz] WIDTH HEIGHT STRIDE out.png
"""

import gzip
import struct
import sys
import zlib


def png(path, raw, w, h, stride):
    """XRGB8888 little-endian (B, G, R, X bytes) to an RGB PNG."""
    rows = bytearray()
    for y in range(h):
        rows.append(0)		# no filter
        line = raw[y * stride:y * stride + w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3] = line[2::4]
        rgb[1::3] = line[1::4]
        rgb[2::3] = line[0::4]
        rows += rgb

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def main():
    if len(sys.argv) != 6:
        sys.exit(__doc__)
    src, w, h, stride, out = sys.argv[1], int(sys.argv[2]), \
        int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
    with open(src, "rb") as f:
        raw = f.read()
    if raw[:2] == b"\x1f\x8b":
        raw = gzip.decompress(raw)
    if len(raw) < (h - 1) * stride + w * 4:
        sys.exit("%d bytes, %d rows of %d need %d" %
                 (len(raw), h, stride, (h - 1) * stride + w * 4))
    png(out, raw, w, h, stride)
    print("written: %s (%dx%d)" % (out, w, h))


if __name__ == "__main__":
    main()
