#!/usr/bin/env python3
"""Replace one file of the ramdisk inside a MEECHO boot archive (boot.mba),
leaving every other byte as it was.

    mba-swap.py ARCHIVE                     list the ramdisk's files
    mba-swap.py ARCHIVE NAME NEWFILE OUT    OUT: ARCHIVE with the file
                                            called NAME (one, anywhere)
                                            holding NEWFILE's bytes
    mba-swap.py ARCHIVE -m IMAGE NEWFILE OUT
                                            OUT: ARCHIVE with the boot
                                            image IMAGE (rs, vm, ...)
                                            holding NEWFILE's bytes, in its
                                            place

Why: the ramdisk is the image the memory driver carries, and a rebuilt one
takes every program of the shared object tree - whatever other work has
put there since.  When one driver changes (sdmmc, for a root on eMMC), the
archive that has been booting is kept, and only that file's contents are
written over, in the blocks it already has: the archive, its images and the
ramdisk keep their sizes, offsets and every other byte.  A file that would
need more blocks than it has is refused.  The result is checked by reading
it back: every other file's md5 the same, NAME's that of NEWFILE.

The same for a boot image with -m (a server that changed, RS for the
zap shader's region): written over where it is, its size in the table
updated, the rest of its old bytes zeroed.  Images start on a page each,
so a new one fits when it ends before the next one starts; one that would
not is refused.  Checked by reading back: every other image the same.

The formats: <machine/bootarchive.h> (a header, then entries of offset,
size and a 32-byte name, little-endian) and MFS V3 (minix/fs/mfs/ondisk.h:
superblock at 1024, 64-byte inodes, 7 direct zones, one indirect, one
double indirect, 64-byte directory entries).
"""
import hashlib
import struct
import sys

MAGIC = 0x3141424D


def entries(a):
    magic, _ver, count, _r, _total = struct.unpack_from("<IIIIQ", a, 0)
    assert magic == MAGIC, "not a boot archive: %x" % magic
    out = {}
    for i in range(count):
        off, size, name = struct.unpack_from("<QQ32s", a, 24 + i * 48)
        out[name.split(b"\0")[0].decode()] = (off, size)
    return out


class Mfs3:
    def __init__(self, buf, base):
        self.buf, self.base = buf, base
        sb = bytes(buf[base + 1024: base + 1088])
        (self.ninodes, _nz, imap, zmap, _fdz, logzone, _fl, _maxs,
         self.zones, magic, _p, self.bsize, _ver) = \
            struct.unpack_from("<IHhhHhHiIhhHB", sb, 0)
        assert magic == 0x4D5A and logzone == 0, "not a V3 image I know"
        self.itab = 2 + imap + zmap

    def blk_off(self, n):
        return self.base + n * self.bsize

    def inode_off(self, i):
        return self.base + self.itab * self.bsize + (i - 1) * 64

    def inode(self, i):
        return struct.unpack_from("<HHhHI3I10I", self.buf, self.inode_off(i))

    def zones_of(self, ino):
        z = list(ino[8:15])
        ind = []
        per = self.bsize // 4
        if ino[15]:
            ind.append(ino[15])
            z += struct.unpack_from("<%dI" % per, self.buf,
                                    self.blk_off(ino[15]))
        if ino[16]:
            ind.append(ino[16])
            for b in struct.unpack_from("<%dI" % per, self.buf,
                                        self.blk_off(ino[16])):
                if b:
                    ind.append(b)
                    z += struct.unpack_from("<%dI" % per, self.buf,
                                            self.blk_off(b))
        return z, ind

    def content(self, ino):
        size = ino[4]
        out = bytearray()
        for z in self.zones_of(ino)[0]:
            if len(out) >= size:
                break
            o = self.blk_off(z)
            out += self.buf[o:o + self.bsize] if z else bytes(self.bsize)
        return bytes(out[:size])

    def walk(self, i=1, path=""):
        ino = self.inode(i)
        mode = ino[0] & 0o170000
        if mode == 0o040000:
            d = self.content(ino)
            for k in range(0, len(d), 64):
                num, name = struct.unpack_from("<I60s", d, k)
                name = name.split(b"\0")[0].decode(errors="replace")
                if num and name not in (".", ".."):
                    yield from self.walk(num, path + "/" + name)
        elif mode in (0o100000, 0o120000):
            yield i, path


def find_mfs(buf, lo, hi):
    """Offsets in [lo, hi) where a V3 superblock would make an image."""
    found = []
    i = buf.find(b"\x5a\x4d", lo, hi)
    while i >= 0:
        base = i - 24 - 1024
        if base >= lo and base + 4096 <= hi:
            bsize, ver = struct.unpack_from("<HB", buf, i + 4)
            if bsize == 4096 and ver in (0, 3):  # mkfs leaves it 0
                found.append(base)
        i = buf.find(b"\x5a\x4d", i + 1, hi)
    return found


def listing(fs):
    return {path: hashlib.md5(fs.content(fs.inode(i))).hexdigest()
            for i, path in fs.walk()}


def swap_image(a, name, new, out):
    """-m: the boot image NAME holding NEW, where it is."""
    ents = entries(a)
    assert name in ents, "no boot image called %s" % name
    off, size = ents[name]
    _m, _v, count, _r, total = struct.unpack_from("<IIIIQ", a, 0)
    room = min([o for o, _s in ents.values() if o > off] + [total]) - off
    if len(new) > room:
        sys.exit("%s: %d bytes of room, %d wanted - refused" %
                 (name, room, len(new)))
    before = {n: hashlib.md5(a[o:o + s]).hexdigest()
              for n, (o, s) in ents.items() if n != name}
    a[off:off + max(size, len(new))] = new + bytes(max(size, len(new)) -
                                                   len(new))
    for i in range(count):
        o, _s, nm = struct.unpack_from("<QQ32s", a, 24 + i * 48)
        if nm.split(b"\0")[0].decode() == name:
            struct.pack_into("<Q", a, 24 + i * 48 + 8, len(new))
    after = entries(a)
    o, s = after[name]
    assert (o, s) == (off, len(new)) and a[o:o + s] == new, "read back"
    for n, md5 in before.items():
        o, s = after[n]
        assert hashlib.md5(a[o:o + s]).hexdigest() == md5, "%s changed" % n
    open(out, "wb").write(a)
    print("%s: %d -> %d bytes at 0x%x (room %d); %d other images as they "
          "were" % (name, size, len(new), off, room, len(before)))


def main():
    a = bytearray(open(sys.argv[1], "rb").read())
    if len(sys.argv) == 6 and sys.argv[2] == "-m":
        swap_image(a, sys.argv[3], open(sys.argv[4], "rb").read(),
                   sys.argv[5])
        return
    off, size = entries(a)["memory"]
    bases = find_mfs(a, off, off + size)
    assert len(bases) == 1, "%d ramdisk images in memory" % len(bases)
    fs = Mfs3(a, bases[0])
    if len(sys.argv) == 2:
        for path, md5 in sorted(listing(fs).items()):
            ino = fs.inode([i for i, p in fs.walk() if p == path][0])
            print("%s %9d %s" % (md5[:8], ino[4], path))
        return
    name, new = sys.argv[2], open(sys.argv[3], "rb").read()
    hits = [(i, p) for i, p in fs.walk() if p.rsplit("/", 1)[-1] == name]
    assert len(hits) == 1, "%d files called %s" % (len(hits), name)
    inum, path = hits[0]
    before = listing(fs)
    ino = fs.inode(inum)
    zones = [z for z in fs.zones_of(ino)[0]
             if z][:(ino[4] + fs.bsize - 1) // fs.bsize]
    need = (len(new) + fs.bsize - 1) // fs.bsize
    if need != len(zones):
        sys.exit("%s: %d blocks, %s needs %d - refused (only the same "
                 "number of blocks)" % (path, len(zones), sys.argv[3], need))
    for k, z in enumerate(zones):
        piece = new[k * fs.bsize:(k + 1) * fs.bsize]
        o = fs.blk_off(z)
        a[o:o + fs.bsize] = piece + bytes(fs.bsize - len(piece))
    struct.pack_into("<I", a, fs.inode_off(inum) + 8, len(new))
    after = listing(Mfs3(a, bases[0]))
    for p in before:
        if p == path:
            assert after[p] == hashlib.md5(new).hexdigest(), "read back"
        else:
            assert after[p] == before[p], "%s changed" % p
    open(sys.argv[4], "wb").write(a)
    print("%s: %d -> %d bytes, %d blocks; %d other files as they were" %
          (path, ino[4], len(new), need, len(before) - 1))


main()
