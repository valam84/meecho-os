#!/usr/bin/env python3
"""Внести в том MFS V4 порчу известного вида - чтобы было что чинить.

Проверять проверяльщик надо порчей, а не чтением: это правило записано ещё на
вехе 7.5, и на хосте девять видов повреждений так и проверялись. Здесь то же
самое делается на плате, только порчу вносит Linux по сырому устройству, а
находит и чинит её fsck_mfs, работающий через свой драйвер на настоящей
флэш-памяти. Тот, кто портит, и тот, кто чинит, не имеют между собой ни
строчки общего кода - в этом половина смысла.

Порча вносится отсюда, а не из MEECHO, ровно по одной причине: точность. dd в
ramdisk умеет блоки, а нужно поле в иноде и число в записи каталога.

    mfs4-damage.py /dev/mmcblk1 [--victim /путь/к/файлу] [что ...]

Что (по умолчанию imap zmap nlinks):
    imap    обнулить битовую карту инодов
    zmap    обнулить битовую карту блоков
    nlinks  выставить корневому каталогу неверное число ссылок
    dangle  направить запись каталога на свободный инод (нужен --victim)
    none    ничего не портить, только показать раскладку
"""

import struct
import sys

SB_OFF = 1024
SB_FMT = "<14IQqqqII"   # всё до s_uuid, ровно 96 байт
I_NLINKS = 4            # смещение i_nlinks в иноде
I_ZONE = 68             # смещение i_zone[] в иноде
NR_DZONES = 12


class Fs(object):
    def __init__(self, path):
        self.f = open(path, "r+b", buffering=0)
        self.f.seek(SB_OFF)
        (self.magic, self.disk_version, self.bsize, self.ninodes,
         self.nblocks, self.firstdata, self.isize, self.imap_blocks,
         self.zmap_blocks, self.flags, self.compat, self.ro_compat,
         self.incompat, self.reserved0, self.max_size, self.mkfs_time,
         self.mount_time, self.write_time, self.journal_inum,
         self.journal_blocks) = struct.unpack(SB_FMT, self.f.read(96))
        if self.magic != 0x3453464D:
            raise SystemExit("не MFS4: магия %#x" % self.magic)
        self.ipb = self.bsize // self.isize
        self.imap_blk = 2
        self.zmap_blk = self.imap_blk + self.imap_blocks
        self.itab_blk = self.zmap_blk + self.zmap_blocks

    def rdblk(self, n):
        self.f.seek(n * self.bsize)
        return bytearray(self.f.read(self.bsize))

    def wrblk(self, n, buf):
        self.f.seek(n * self.bsize)
        self.f.write(bytes(buf))

    def inode_off(self, ino):
        blk = self.itab_blk + (ino - 1) // self.ipb
        return blk * self.bsize + ((ino - 1) % self.ipb) * self.isize

    def rdinode(self, ino):
        self.f.seek(self.inode_off(ino))
        return bytearray(self.f.read(self.isize))

    def wrinode(self, ino, buf):
        self.f.seek(self.inode_off(ino))
        self.f.write(bytes(buf))

    def zones(self, inobuf):
        """Прямые блоки инода. Косвенности здесь не нужны: портятся малые
        каталоги, а они в двенадцать прямых блоков помещаются целиком."""
        return [struct.unpack_from("<I", inobuf, I_ZONE + 4 * i)[0]
                for i in range(NR_DZONES)]

    def dirents(self, blk):
        """(смещение в блоке, d_ino, имя) для каждой записи блока."""
        buf = self.rdblk(blk)
        pos = 0
        out = []
        while pos + 8 <= self.bsize:
            ino, reclen, namelen, dtype = struct.unpack_from("<IHBB", buf, pos)
            if reclen < 8 or pos + reclen > self.bsize:
                break
            out.append((pos, ino, bytes(buf[pos + 8:pos + 8 + namelen])
                        .decode("latin-1")))
            pos += reclen
        return buf, out

    def find(self, dir_ino, name):
        """(блок, смещение, инод) записи name в каталоге dir_ino."""
        for blk in self.zones(self.rdinode(dir_ino)):
            if blk == 0:
                continue
            _, ents = self.dirents(blk)
            for pos, ino, nm in ents:
                if ino != 0 and nm == name:
                    return blk, pos, ino
        return None

    def lookup(self, path):
        """Путь от корня к (блок каталога, смещение записи, инод)."""
        ino = 1
        found = None
        for part in [p for p in path.split("/") if p]:
            found = self.find(ino, part)
            if found is None:
                raise SystemExit("нет такого пути: %s (на %s)" % (path, part))
            ino = found[2]
        if found is None:
            raise SystemExit("--victim должен называть файл, а не корень")
        return found


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    argv = sys.argv[1:]
    dev = argv.pop(0)
    victim_path = None
    if argv and argv[0] == "--victim":
        argv.pop(0)
        victim_path = argv.pop(0)
    what = argv or ["imap", "zmap", "nlinks"]

    fs = Fs(dev)
    print("том: блок %d, инодов %d, блоков %d, imap@%d(%d) zmap@%d(%d) "
          "itab@%d, журнал в иноде %d"
          % (fs.bsize, fs.ninodes, fs.nblocks, fs.imap_blk, fs.imap_blocks,
             fs.zmap_blk, fs.zmap_blocks, fs.itab_blk, fs.journal_inum))

    rblk = fs.zones(fs.rdinode(1))[0]
    _, ents = fs.dirents(rblk)
    print("корень в блоке %d: %s" % (rblk, " ".join(
        "%s(%d)" % (n or "-", i) for _, i, n in ents)))

    if "nlinks" in what:
        # Корень, а не файл: файл из "dangle" становится потерянным и уходит к
        # проходу 4, так что счётчик ссылок на нём проход 3 уже не увидит. У
        # корня же ссылки есть всегда - "." и ".." его самого.
        b = fs.rdinode(1)
        old = struct.unpack_from("<I", b, I_NLINKS)[0]
        struct.pack_into("<I", b, I_NLINKS, 7)
        fs.wrinode(1, b)
        print("nlinks: корневой инод 1: %d -> 7" % old)

    if "dangle" in what:
        if victim_path is None:
            raise SystemExit("dangle без --victim: нечего портить")
        blk, pos, ino = fs.lookup(victim_path)
        free = fs.ninodes - 1           # заведомо свободный инод
        buf = fs.rdblk(blk)
        struct.pack_into("<I", buf, pos, free)
        fs.wrblk(blk, buf)
        print("dangle: %s: ino %d -> %d (свободный)" % (victim_path, ino, free))

    if "imap" in what:
        for n in range(fs.imap_blk, fs.imap_blk + fs.imap_blocks):
            fs.wrblk(n, bytearray(fs.bsize))
        print("imap: обнулены блоки %d..%d"
              % (fs.imap_blk, fs.imap_blk + fs.imap_blocks - 1))

    if "zmap" in what:
        for n in range(fs.zmap_blk, fs.zmap_blk + fs.zmap_blocks):
            fs.wrblk(n, bytearray(fs.bsize))
        print("zmap: обнулены блоки %d..%d"
              % (fs.zmap_blk, fs.zmap_blk + fs.zmap_blocks - 1))

    fs.f.flush()
    fs.f.close()
    print("готово")
    return 0


if __name__ == "__main__":
    sys.exit(main())
