#!/usr/bin/env python3
"""Take a clover boot.img apart and check every piece against its source.

	checkboot.py boot.img [--kernel kernel.bin] [--dtb clover.dtb]
	                      [--ramdisk boot.mba] [--partition-size 0x4000000]

Before a boot.img is written to the tablet's boot partition - not merely
started with "fastboot boot" - it is read back the way the ABL reads it:
the v0 header (magic, load addresses, page size, the SHA-1 id over the
sections), the kernel section as gzip followed by a device tree, the arm64
header inside the kernel (magic, text_offset, flags, image_size), and the
ramdisk. Each piece given on the command line must match byte for byte.
The image must fit the partition: 64 MiB on clover, BOARD_BOOTIMAGE_
PARTITION_SIZE in device/xiaomi/sdm660-common (LineageOS 22.2).

Exit status 0 only if every check passed.
"""
import argparse, gzip, hashlib, io, struct, sys, zlib

p = argparse.ArgumentParser()
p.add_argument("image")
p.add_argument("--kernel")
p.add_argument("--dtb")
p.add_argument("--ramdisk")
p.add_argument("--partition-size", type=lambda s: int(s, 0), default=0x4000000)
a = p.parse_args()

bad = 0
def check(ok, what):
	global bad
	print("%-4s %s" % ("ok" if ok else "FAIL", what))
	if not ok:
		bad += 1

img = open(a.image, "rb").read()
check(img[:8] == b"ANDROID!", "magic ANDROID!")
(ksize, kaddr, rsize, raddr, ssize, saddr, tags, page, hver, osver) = \
    struct.unpack_from("<10I", img, 8)
name = img[48:64].rstrip(b"\0")
cmd = img[64:576].rstrip(b"\0") + img[608:1632].rstrip(b"\0")
ident = img[576:608]
print("     kernel %d @%#x, ramdisk %d @%#x, second %d @%#x, tags @%#x" %
    (ksize, kaddr, rsize, raddr, ssize, saddr, tags))
print("     page %d, header v%d, os_version %#x, name %r" %
    (page, hver, osver, name.decode()))
print("     cmdline %r" % cmd.decode())
check(page == 4096, "page size 4096 (BOARD_KERNEL_PAGESIZE)")
check(hver == 0, "header version 0 (none set for clover)")
check((kaddr, raddr, saddr, tags) == (0x8000, 0x1000000, 0xf00000, 0x100),
    "offsets as postmarketOS deviceinfo, base 0 (BOARD_KERNEL_BASE)")
check(ssize == 0, "no second stage")
check(len(img) <= a.partition_size,
    "%d bytes fit the boot partition (%d)" % (len(img), a.partition_size))

def pages(n):
	return (n + page - 1) // page * page
koff = page
roff = koff + pages(ksize)
check(len(img) >= roff + rsize, "image long enough for its sections")
ksec = img[koff:koff + ksize]
rsec = img[roff:roff + rsize]

sha = hashlib.sha1()
for blob in (ksec, rsec, b""):
	sha.update(blob)
	sha.update(struct.pack("<I", len(blob)))
check(ident[:20] == sha.digest(), "id is the SHA-1 of the sections")

# The kernel section: a gzip stream, then the device tree.
check(ksec[:2] == b"\x1f\x8b", "kernel section starts with gzip")
d = zlib.decompressobj(16 + zlib.MAX_WBITS)
kernel = d.decompress(ksec)
dtb = d.unused_data
check(d.eof, "gzip stream complete")
check(dtb[:4] == b"\xd0\x0d\xfe\xed", "device tree appended after gzip")
if dtb[:4] == b"\xd0\x0d\xfe\xed":
	tot = struct.unpack(">I", dtb[4:8])[0]
	check(tot == len(dtb), "device tree totalsize %d = what follows" % tot)
	check(b"xiaomi,clover" in dtb and b"simple-framebuffer" in dtb,
	    "tree is clover's and has the simple-framebuffer")

check(kernel[56:60] == b"ARM\x64", "arm64 image magic")
text_offset, image_size, flags = struct.unpack_from("<QQQ", kernel, 8)
print("     arm64 header: text_offset %#x, image_size %#x, flags %#x" %
    (text_offset, image_size, flags))
check(text_offset == 0, "text_offset 0 (as Linux; what the ABL boots)")
check(flags & 8, "flags bit 3: any physical base")
check(image_size >= len(kernel), "image_size covers the file (%d)" % len(kernel))

if a.kernel:
	check(kernel == open(a.kernel, "rb").read(), "kernel == %s" % a.kernel)
if a.dtb:
	check(dtb == open(a.dtb, "rb").read(), "dtb == %s" % a.dtb)
if a.ramdisk:
	check(rsec == open(a.ramdisk, "rb").read(), "ramdisk == %s" % a.ramdisk)

print("%d check%s failed" % (bad, "" if bad == 1 else "s") if bad else
    "all checks passed")
sys.exit(1 if bad else 0)
