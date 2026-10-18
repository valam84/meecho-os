#!/usr/bin/env python3
"""The on-screen keyboard's font for fb's console: osk_font.h.

    mkoskfont.py FONT.ttf > osk_font.h

fb has no rasteriser and wants none, so the keys' letters come
pre-rendered: printable ASCII from a TrueType font (Inter Regular,
external/ofl/inter, OFL - the result is a rendering of it and goes with
its licence) at a few sizes, anti-aliased, 4 bits of coverage a pixel,
plus the keys' icons - the arrows as chevrons, Shift, Backspace, Enter -
drawn here as polygons in the same rasteriser.  The keyboard picks the
largest size that fits its keys (fbcon.c).

Nothing outside the standard library: a TrueType reader (cmap 4, glyf
with composites, hmtx), quadratic curves flattened, a non-zero scanline
fill with 16 sub-rows a pixel and exact coverage along each row.  Run it
again when the sizes, the icons or the font change; the output is kept
in the tree next to fbcon.c so that building fb needs none of this.
"""
import math
import struct
import sys

SIZES = (18, 24, 32, 42, 54, 64)	# em, pixels
SUB = 16				# sub-rows a pixel
M = 4					# room round a glyph: bearings < 0

# ---------------------------------------------------------------- TrueType

class Font:
	def __init__(self, data):
		self.d = data
		n = struct.unpack(">H", data[4:6])[0]
		self.t = {}
		for i in range(n):
			tag, _, off, ln = struct.unpack(">4sIII",
			    data[12 + 16 * i:28 + 16 * i])
			self.t[tag.decode()] = (off, ln)
		h = self.tab("head")
		self.upem = struct.unpack(">H", h[18:20])[0]
		self.locfmt = struct.unpack(">h", h[50:52])[0]
		self.nglyphs = struct.unpack(">H", self.tab("maxp")[4:6])[0]
		hh = self.tab("hhea")
		self.ascent, self.descent = struct.unpack(">hh", hh[4:8])
		self.nhm = struct.unpack(">H", hh[34:36])[0]
		self.cmap = self.read_cmap()

	def tab(self, name):
		off, ln = self.t[name]
		return self.d[off:off + ln]

	def read_cmap(self):
		c = self.tab("cmap")
		n = struct.unpack(">H", c[2:4])[0]
		for i in range(n):
			pid, eid, off = struct.unpack(">HHI", c[4 + 8 * i:12 + 8 * i])
			if pid == 3 and eid == 1 and \
			    struct.unpack(">H", c[off:off + 2])[0] == 4:
				return self.cmap4(c[off:])
		raise SystemExit("no Unicode BMP cmap")

	def cmap4(self, s):
		segx2 = struct.unpack(">H", s[6:8])[0]
		seg = segx2 // 2
		ends = struct.unpack(">%dH" % seg, s[14:14 + segx2])
		p = 16 + segx2
		starts = struct.unpack(">%dH" % seg, s[p:p + segx2])
		p += segx2
		deltas = struct.unpack(">%dh" % seg, s[p:p + segx2])
		ro_at = p + segx2
		ros = struct.unpack(">%dH" % seg, s[ro_at:ro_at + segx2])
		m = {}
		for i in range(seg):
			for ch in range(starts[i], ends[i] + 1):
				if ch == 0xffff:
					continue
				if ros[i] == 0:
					g = (ch + deltas[i]) & 0xffff
				else:
					a = ro_at + 2 * i + ros[i] + 2 * (ch - starts[i])
					g = struct.unpack(">H", s[a:a + 2])[0]
					if g:
						g = (g + deltas[i]) & 0xffff
				m[ch] = g
		return m

	def advance(self, g):
		hm = self.tab("hmtx")
		i = min(g, self.nhm - 1)
		return struct.unpack(">H", hm[4 * i:4 * i + 2])[0]

	def glyph_data(self, g):
		loca = self.tab("loca")
		if self.locfmt == 0:
			a, b = (2 * x for x in struct.unpack(">HH",
			    loca[2 * g:2 * g + 4]))
		else:
			a, b = struct.unpack(">II", loca[4 * g:4 * g + 8])
		return self.tab("glyf")[a:b]

	def contours(self, g, depth=0):
		"""The outline as polygons, font units, y up."""
		s = self.glyph_data(g)
		if not s:
			return []
		nc = struct.unpack(">h", s[0:2])[0]
		if nc < 0:
			return self.composite(s, depth)
		ends = struct.unpack(">%dH" % nc, s[10:10 + 2 * nc])
		p = 10 + 2 * nc
		ilen = struct.unpack(">H", s[p:p + 2])[0]
		p += 2 + ilen
		npts = ends[-1] + 1 if nc else 0
		flags = []
		while len(flags) < npts:
			f = s[p]
			p += 1
			flags.append(f)
			if f & 8:
				r = s[p]
				p += 1
				flags.extend([f] * r)
		xs, ys = [], []
		for coords, short, same in ((xs, 2, 16), (ys, 4, 32)):
			v = 0
			for f in flags:
				if f & short:
					d = s[p]
					p += 1
					v += d if f & same else -d
				elif not f & same:
					v += struct.unpack(">h", s[p:p + 2])[0]
					p += 2
				coords.append(v)
		out, start = [], 0
		for e in ends:
			pts = [(xs[i], ys[i], flags[i] & 1)
			    for i in range(start, e + 1)]
			start = e + 1
			out.append(flatten(pts))
		return out

	def composite(self, s, depth):
		p, out = 10, []
		while True:
			fl, gi = struct.unpack(">HH", s[p:p + 4])
			p += 4
			if fl & 1:
				dx, dy = struct.unpack(">hh", s[p:p + 4])
				p += 4
			else:
				dx, dy = struct.unpack(">bb", s[p:p + 2])
				p += 2
			a, b, c, d = 1.0, 0.0, 0.0, 1.0
			if fl & 8:
				a = d = struct.unpack(">h", s[p:p + 2])[0] / 16384
				p += 2
			elif fl & 0x40:
				a, d = (v / 16384 for v in
				    struct.unpack(">hh", s[p:p + 4]))
				p += 4
			elif fl & 0x80:
				a, b, c, d = (v / 16384 for v in
				    struct.unpack(">hhhh", s[p:p + 8]))
				p += 8
			if not fl & 2:
				dx = dy = 0	# point matching: not in ASCII
			for poly in self.contours(gi, depth + 1):
				out.append([(a * x + c * y + dx, b * x + d * y + dy)
				    for x, y in poly])
			if not fl & 0x20:
				break
		return out


def flatten(pts):
	"""A TrueType contour (on/off-curve points) as a polyline."""
	n = len(pts)
	if n == 0:
		return []
	# Start on an on-curve point; two off-curve points imply one between.
	full = []
	for i in range(n):
		x, y, on = pts[i]
		nx, ny, non = pts[(i + 1) % n]
		full.append((x, y, on))
		if not on and not non:
			full.append(((x + nx) / 2, (y + ny) / 2, 1))
	k = next((i for i, q in enumerate(full) if q[2]), None)
	if k is None:
		return []
	full = full[k:] + full[:k]
	out = [(full[0][0], full[0][1])]
	i, m = 0, len(full)
	while i < m:
		x0, y0, _ = full[i]
		x1, y1, on1 = full[(i + 1) % m]
		if on1:
			out.append((x1, y1))
			i += 1
			continue
		x2, y2, _ = full[(i + 2) % m]
		for t in range(1, 9):
			t /= 8
			u = 1 - t
			out.append((u * u * x0 + 2 * u * t * x1 + t * t * x2,
			    u * u * y0 + 2 * u * t * y1 + t * t * y2))
		i += 2
	return out

# ---------------------------------------------------------------- the fill

def raster(polys, w, h):
	"""Polygons in pixels (y down) to coverage 0..1, w by h, non-zero."""
	edges = []
	for poly in polys:
		for i in range(len(poly)):
			x0, y0 = poly[i]
			x1, y1 = poly[(i + 1) % len(poly)]
			if y0 == y1:
				continue
			wdir = 1 if y1 > y0 else -1
			if y0 > y1:
				x0, y0, x1, y1 = x1, y1, x0, y0
			edges.append((y0, y1, x0, (x1 - x0) / (y1 - y0), wdir))
	cov = [[0.0] * w for _ in range(h)]
	for row in range(h):
		acc = cov[row]
		for k in range(SUB):
			y = row + (k + 0.5) / SUB
			xs = sorted((x0 + (y - y0) * dx, wd)
			    for y0, y1, x0, dx, wd in edges if y0 <= y < y1)
			wind = 0
			for j in range(len(xs) - 1):
				wind += xs[j][1]
				if wind == 0:
					continue
				span(acc, xs[j][0], xs[j + 1][0], w)
	return [[min(1.0, v / SUB) for v in r] for r in cov]


def span(acc, a, b, w):
	a, b = max(a, 0.0), min(b, float(w))
	if b <= a:
		return
	ia, ib = int(a), int(b)
	if ia == ib:
		acc[ia] += b - a
		return
	acc[ia] += ia + 1 - a
	for i in range(ia + 1, min(ib, w)):
		acc[i] += 1
	if ib < w:
		acc[ib] += b - ib

# ---------------------------------------------------------------- icons

def rev(p):
	return list(reversed(p))


def circle(cx, cy, r, n=48):
	return [(cx + r * math.cos(2 * math.pi * i / n),
	    cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def chevron(t):
	"""Pointing up, in a unit box, stroke t: rotate for the others."""
	return [(0.5, 0.22), (0.92, 0.64), (0.92 - t, 0.64 + t * 0.75),
	    (0.5, 0.22 + t * 1.45), (0.08 + t, 0.64 + t * 0.75), (0.08, 0.64)]


def turn(poly, quarter):
	for _ in range(quarter):
		poly = [(1 - y, x) for x, y in poly]
	return poly

# Unit-box icons (y down), keyed by the glyph libosk names.
ICONS = {
	"up": [chevron(0.2)],
	"right": [turn(chevron(0.2), 1)],
	"down": [turn(chevron(0.2), 2)],
	"left": [turn(chevron(0.2), 3)],
	# A disc with an arrow cut out of it, as Buffyboard's Shift.
	"shift": [circle(0.5, 0.5, 0.48),
	    rev([(0.5, 0.18), (0.80, 0.52), (0.63, 0.52), (0.63, 0.80),
	    (0.37, 0.80), (0.37, 0.52), (0.20, 0.52)])],
	# A tag pointing left with a cross cut out of it.
	"bksp": [[(0.0, 0.5), (0.28, 0.16), (1.0, 0.16), (1.0, 0.84),
	    (0.28, 0.84)],
	    rev([(a + 0.62, b + 0.5) for a, b in (
	    (-0.17, -0.22), (0.0, -0.06), (0.17, -0.22), (0.22, -0.17),
	    (0.06, 0.0), (0.22, 0.17), (0.17, 0.22), (0.0, 0.06),
	    (-0.17, 0.22), (-0.22, 0.17), (-0.06, 0.0), (-0.22, -0.17))])],
	"enter": [[(0.04, 0.52), (0.16, 0.40), (0.38, 0.62), (0.84, 0.16),
	    (0.96, 0.28), (0.38, 0.86)]],
}
ICON_ORDER = ("up", "down", "left", "right", "shift", "bksp", "enter")

# ---------------------------------------------------------------- output

def bitmap(cov):
	"""Trim to the ink, 4 bits a pixel, two to a byte, rows whole bytes."""
	h = len(cov)
	w = len(cov[0]) if h else 0
	q = [[min(15, int(v * 15 + 0.5)) for v in r] for r in cov]
	rows = [i for i in range(h) if any(q[i])]
	cols = [j for j in range(w) if any(q[i][j] for i in range(h))]
	if not rows:
		return 0, 0, 0, 0, b""
	r0, r1, c0, c1 = rows[0], rows[-1] + 1, cols[0], cols[-1] + 1
	out = bytearray()
	for i in range(r0, r1):
		r = q[i][c0:c1] + [0]
		for j in range(0, c1 - c0, 2):
			out.append(r[j] << 4 | r[j + 1])
	return c1 - c0, r1 - r0, c0, r0, bytes(out)


def main():
	f = Font(open(sys.argv[1], "rb").read())
	glyphs, bits = [], bytearray()
	sizes = []
	for px in SIZES:
		sc = px / f.upem
		asc = int(math.ceil(f.ascent * sc))
		height = asc + int(math.ceil(-f.descent * sc))
		first = len(glyphs)
		for ch in range(32, 127):
			g = f.cmap.get(ch, 0)
			polys = [[(x * sc + M, asc - y * sc + M) for x, y in p]
			    for p in f.contours(g)]
			W = int(f.advance(g) * sc) + 2 * M
			cov = raster(polys, W, height + 2 * M) if polys else [[0]]
			w, h, x0, y0, b = bitmap(cov)
			glyphs.append((w, h, x0 - M, y0 - M,
			    int(round(f.advance(g) * sc)), len(bits)))
			bits += b
		box = int(round(px * 0.78))		# icons: a letter key
		for name in ICON_ORDER:
			polys = [[(x * box, y * box) for x, y in p]
			    for p in ICONS[name]]
			w, h, x0, y0, b = bitmap(raster(polys, box, box))
			glyphs.append((w, h, x0, y0, box, len(bits)))
			bits += b
		sizes.append((px, asc, height, first))

	o = sys.stdout
	o.write("/*\n * Generated by mkoskfont.py from %s - do not edit.\n"
	    " * Inter: SIL Open Font License 1.1 (external/ofl/inter).\n"
	    " */\n" % sys.argv[1].split("/")[-1])
	o.write("#define OSKF_FIRST\t32\t/* ' ' */\n#define OSKF_NCHARS\t95\n")
	for i, n in enumerate(ICON_ORDER):
		o.write("#define OSKF_ICON_%s\t%d\n" % (n.upper(), 95 + i))
	o.write("#define OSKF_NGLYPHS\t%d\n" % (95 + len(ICON_ORDER)))
	o.write("#define OSKF_NSIZES\t%d\n\n" % len(sizes))
	o.write("struct oskf_size {\n\tuint8_t px, ascent, height;\n"
	    "\tuint16_t first;\n};\n")
	o.write("/* w, h: the ink; x, y: from the pen at the line's top. */\n")
	o.write("struct oskf_glyph {\n\tuint8_t w, h;\n\tint8_t x, y;\n"
	    "\tuint8_t advance;\n\tuint32_t off;\n};\n\n")
	o.write("static const struct oskf_size oskf_sizes[OSKF_NSIZES] = {\n")
	for s in sizes:
		o.write("\t{ %d, %d, %d, %d },\n" % s)
	o.write("};\n\nstatic const struct oskf_glyph oskf_glyphs[] = {\n")
	for g in glyphs:
		o.write("\t{ %d, %d, %d, %d, %d, %d },\n" % g)
	o.write("};\n\n/* 4 bits of coverage a pixel, high nibble first. */\n")
	o.write("static const uint8_t oskf_bits[%d] = {\n" % len(bits))
	for i in range(0, len(bits), 16):
		o.write("\t" + ", ".join("0x%02x" % v for v in bits[i:i + 16]) +
		    ",\n")
	o.write("};\n")


if __name__ == "__main__":
	main()
