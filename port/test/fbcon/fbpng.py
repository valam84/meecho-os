#!/usr/bin/env python3
"""Raw x8r8g8b8 frame buffer -> PNG, with nothing but the standard library.

	fbpng.py RAW WIDTH HEIGHT OUT.png [SCALE]

SCALE (default 0.5, i.e. every other pixel) keeps the picture small enough to
look at; the console draws its font doubled on a panel this dense, so halving
loses nothing a reader needs.
"""
import struct, sys, zlib

raw, w, h, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
step = int(round(1 / float(sys.argv[5]))) if len(sys.argv) > 5 else 2
data = open(raw, "rb").read()
if len(data) < w * h * 4:
	sys.exit("%s: %d bytes, want %d" % (raw, len(data), w * h * 4))

rows = []
for y in range(0, h, step):
	line = data[y * w * 4:(y + 1) * w * 4]
	px = bytearray()
	for x in range(0, w, step):
		b, g, r = line[x * 4], line[x * 4 + 1], line[x * 4 + 2]
		px += bytes((r, g, b))
	rows.append(b"\0" + bytes(px))

ow, oh = len(range(0, w, step)), len(rows)
def chunk(kind, body):
	c = struct.pack(">I", len(body)) + kind + body
	return c + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", ow, oh,
    8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"".join(rows), 6)) + \
    chunk(b"IEND", b"")
open(out, "wb").write(png)
