#!/usr/bin/env python3
"""Образ SD-карты для стенда sunxi и проверка его после прогона.

    mkimg.py make  образ байт     разметить первые и последние 2048 секторов
    mkimg.py check образ          проверить то, что стенд записал

Узор тот же, что в harness.c: слово w сектора s - (s*0x9e3779b1 +
w*0x85ebca77) ^ seed. Проверка после прогона - то, чего стенд изнутри
доказать не может: что записанное легло на носитель (в файл образа, который
держит модель карты QEMU), и легло туда, куда просили, не задев соседей.
"""
import os
import struct
import sys

SEED_IMAGE = 0xA5A5A5A5
SEED_WRITE = 0x3C3C3C3C
PATTERNED = 2048
SECTOR = 512


def sector(s, seed):
    return struct.pack("<128I", *[((s * 0x9E3779B1 + w * 0x85EBCA77)
                                   & 0xFFFFFFFF) ^ seed for w in range(128)])


def make(path, size):
    n = size // SECTOR
    with open(path, "wb") as f:
        f.truncate(size)
        for first in (0, n - PATTERNED):
            f.seek(first * SECTOR)
            f.write(b"".join(sector(s, SEED_IMAGE)
                             for s in range(first, first + PATTERNED)))


def check(path):
    n = os.path.getsize(path) // SECTOR
    written = set(range(1024, 1088)) | {1100, 1200, 1201, 1202, n - 1}
    bad = 0
    with open(path, "rb") as f:
        for first in (0, n - PATTERNED):
            f.seek(first * SECTOR)
            for s in range(first, first + PATTERNED):
                data = f.read(SECTOR)
                want = sector(s, SEED_WRITE if s in written else SEED_IMAGE)
                if data != want:
                    bad += 1
                    if bad <= 5:
                        print("image: sector %d is not what it should be" % s)
    print("image: %d sectors checked, %d written by the harness, %d bad"
          % (2 * PATTERNED, len(written), bad))
    return bad == 0


if __name__ == "__main__":
    if sys.argv[1] == "make":
        make(sys.argv[2], int(sys.argv[3]))
    else:
        sys.exit(0 if check(sys.argv[2]) else 1)
