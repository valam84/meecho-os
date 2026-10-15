#!/usr/bin/env python3
"""An Android boot image, header version 0, as a Qualcomm ABL boots it.

	mkbootimg.py --kernel K --ramdisk R --cmdline "..." -o boot.img

Header v0 because that is what the Mi Pad 4's ABL takes and what
postmarketOS builds for it (device-xiaomi-clover/deviceinfo): page size 4096,
base 0, kernel at +0x8000, ramdisk at +0x1000000, second at +0xf00000, tags
at +0x100. The device tree is not a section of its own in v0; it is appended
to the kernel (mkboot.sh does that), and the ABL finds it there.

Written here rather than taken from AOSP's mkbootimg so that building the
image needs nothing but python3; the layout is the one in AOSP's
include/bootimg/bootimg.h, struct boot_img_hdr_v0.
"""
import argparse, hashlib, struct

p = argparse.ArgumentParser()
p.add_argument("--kernel", required=True)
p.add_argument("--ramdisk", required=True)
p.add_argument("--cmdline", default="")
p.add_argument("--base", type=lambda s: int(s, 0), default=0)
p.add_argument("--kernel-offset", type=lambda s: int(s, 0), default=0x8000)
p.add_argument("--ramdisk-offset", type=lambda s: int(s, 0), default=0x1000000)
p.add_argument("--second-offset", type=lambda s: int(s, 0), default=0xf00000)
p.add_argument("--tags-offset", type=lambda s: int(s, 0), default=0x100)
p.add_argument("--pagesize", type=int, default=4096)
p.add_argument("--name", default="meecho")
p.add_argument("-o", "--output", required=True)
a = p.parse_args()

kernel = open(a.kernel, "rb").read()
ramdisk = open(a.ramdisk, "rb").read()
cmd = a.cmdline.encode()
if len(cmd) > 512 + 1024 - 2:
	raise SystemExit("cmdline too long")
cmd1, cmd2 = cmd[:511], cmd[511:]

# The id: SHA-1 over each section and its size, as AOSP computes it.
sha = hashlib.sha1()
for blob in (kernel, ramdisk, b""):
	sha.update(blob)
	sha.update(struct.pack("<I", len(blob)))
ident = sha.digest().ljust(32, b"\0")

hdr = b"ANDROID!" + struct.pack("<10I",
    len(kernel), a.base + a.kernel_offset,
    len(ramdisk), a.base + a.ramdisk_offset,
    0, a.base + a.second_offset,
    a.base + a.tags_offset, a.pagesize,
    0,		# header_version
    0)		# os_version: unset, which an unlocked ABL accepts
hdr += a.name.encode()[:15].ljust(16, b"\0")
hdr += cmd1.ljust(512, b"\0")
hdr += ident
hdr += cmd2.ljust(1024, b"\0")

def pad(b):
	return b + b"\0" * (-len(b) % a.pagesize)

with open(a.output, "wb") as f:
	f.write(pad(hdr))
	f.write(pad(kernel))
	f.write(pad(ramdisk))
