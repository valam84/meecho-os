#!/usr/bin/env python3
"""Files out of an ext4 partition that is far away and slow to read.

    ext4-pull.py CACHE PATH... -- FETCH...

FETCH is a command that writes COUNT 4 KiB blocks of the partition from
block SKIP to its stdout: "{skip}" and "{count}" in it are replaced, e.g.
    ssh.exe -J cb2-hub root@10.42.0.2 dd if=/dev/c0d0p3 bs=4096 \
        skip={skip} count={count}
Only the blocks a lookup needs are fetched - superblock, group
descriptors, inode table blocks, directory and extent blocks, the files'
own blocks - and every block fetched is kept in CACHE (a sparse image of
the partition, CACHE.have its block list), so a second run asks for
nothing it has.  PATH (e.g. /firmware/a512_zap.mdt; a trailing "*" takes
every name with that prefix in the directory) is written next to CACHE
under its own name.

Why: the Mi Pad 4's vendor partition is 2 GiB behind a link of ~100 KiB/s
(hw-gpu.md, §14), and the GPU's zap shader in it is some tens of KiB.
CACHE is then a valid ext4 image for the blocks that matter: debugfs's
"dump" on it gives the same files, which is how this reader is checked by
one that shares nothing with it.

ext4 as used here: 4 KiB blocks, extents (with an index or not), 32- or
64-byte group descriptors, linear and htree directories read linearly.
"""
import os
import struct
import subprocess
import sys

BS = 4096


class Remote:
    def __init__(self, cache, fetch):
        self.cache, self.fetch = cache, fetch
        self.have = set()
        if os.path.exists(cache + ".have"):
            self.have = {int(x) for x in open(cache + ".have").read().split()}
        self.f = open(cache, "r+b" if os.path.exists(cache) else "w+b")
        self.calls = 0

    def blocks(self, first, count):
        want = [b for b in range(first, first + count) if b not in self.have]
        while want:
            lo = want[0]
            n = 1
            while n < len(want) and want[n] == lo + n and n < 256:
                n += 1
            cmd = [c.replace("{skip}", str(lo)).replace("{count}", str(n))
                   for c in self.fetch]
            data = subprocess.run(cmd, stdout=subprocess.PIPE,
                                  check=True).stdout
            self.calls += 1
            if len(data) != n * BS:
                sys.exit("fetch of %d blocks at %d gave %d bytes" %
                         (n, lo, len(data)))
            self.f.seek(lo * BS)
            self.f.write(data)
            self.have.update(range(lo, lo + n))
            want = want[n:]
        open(self.cache + ".have", "w").write(
            " ".join(str(b) for b in sorted(self.have)))
        self.f.flush()
        self.f.seek(first * BS)
        return self.f.read(count * BS)


class Ext4:
    def __init__(self, dev):
        self.dev = dev
        sb = dev.blocks(0, 1)[1024:2048]
        (self.inodes_count,) = struct.unpack_from("<I", sb, 0)
        log_bs, = struct.unpack_from("<I", sb, 24)
        self.per_group, = struct.unpack_from("<I", sb, 40)
        magic, = struct.unpack_from("<H", sb, 56)
        self.isize, = struct.unpack_from("<H", sb, 88)
        incompat, = struct.unpack_from("<I", sb, 96)
        dsz, = struct.unpack_from("<H", sb, 254)
        assert magic == 0xEF53, "not ext4"
        assert 1024 << log_bs == BS, "only 4 KiB blocks"
        self.dsize = dsz if incompat & 0x80 else 32
        self.first_data, = struct.unpack_from("<I", sb, 20)

    def inode(self, n):
        g, i = divmod(n - 1, self.per_group)
        gdt = self.first_data + 1
        off = g * self.dsize
        d = self.dev.blocks(gdt + off // BS, 1)[off % BS:off % BS + self.dsize]
        lo, = struct.unpack_from("<I", d, 8)
        hi = struct.unpack_from("<I", d, 0x28)[0] if self.dsize >= 64 else 0
        table = lo | hi << 32
        boff = i * self.isize
        blk = self.dev.blocks(table + boff // BS, 1)
        return blk[boff % BS:boff % BS + self.isize]

    def extents(self, node):
        magic, entries, _max, depth = struct.unpack_from("<HHHH", node, 0)
        assert magic == 0xF30A, "not an extent node"
        out = []
        for k in range(entries):
            e = node[12 + 12 * k:24 + 12 * k]
            if depth == 0:
                lblk, ln, hi, lo = struct.unpack_from("<IHHI", e, 0)
                if ln > 32768:
                    ln -= 32768		# uninitialised: zeros
                out.append((lblk, lo | hi << 32, ln))
            else:
                _lblk, lo, hi = struct.unpack_from("<IIH", e, 0)
                out += self.extents(self.dev.blocks(lo | hi << 32, 1))
        return out

    def content(self, ino):
        size = struct.unpack_from("<I", ino, 4)[0] | \
            struct.unpack_from("<I", ino, 108)[0] << 32
        flags, = struct.unpack_from("<I", ino, 32)
        assert flags & 0x80000, "only files with extents"
        data = bytearray()
        for lblk, pblk, ln in sorted(self.extents(ino[40:100])):
            if len(data) < lblk * BS:
                data += bytes(lblk * BS - len(data))
            n = min(ln, (size - len(data) + BS - 1) // BS)
            if n > 0:
                data += self.dev.blocks(pblk, n)
        return bytes(data[:size])

    def lookup(self, path):
        n = 2
        for part in [p for p in path.split("/") if p]:
            ents = self.dir(n)
            if part not in ents:
                return None
            n = ents[part]
        return n

    def dir(self, n):
        d = self.content(self.inode(n))
        out, k = {}, 0
        while k + 8 <= len(d):
            ino, rec, nl = struct.unpack_from("<IHB", d, k)
            if rec < 8:
                break
            if ino:
                out[d[k + 8:k + 8 + nl].decode(errors="replace")] = ino
            k += rec
        return out


def main():
    sep = sys.argv.index("--")
    cache, paths, fetch = sys.argv[1], sys.argv[2:sep], sys.argv[sep + 1:]
    dev = Remote(cache, fetch)
    fs = Ext4(dev)
    outdir = os.path.dirname(os.path.abspath(cache))
    for p in paths:
        names = [p]
        if p.endswith("*"):
            d, pre = p.rsplit("/", 1)
            n = fs.lookup(d)
            if n is None:
                print("%s: no such directory" % d)
                continue
            names = [d + "/" + x for x in sorted(fs.dir(n))
                     if x.startswith(pre[:-1])]
        for q in names:
            n = fs.lookup(q)
            if n is None:
                print("%s: not there" % q)
                continue
            data = fs.content(fs.inode(n))
            open(os.path.join(outdir, q.rsplit("/", 1)[-1]), "wb").write(data)
            print("%s: inode %d, %d bytes" % (q, n, len(data)))
    print("%d fetches, %d blocks cached" % (dev.calls, len(dev.have)))


main()
