#!/usr/bin/env python3
# Слить СЫРЫЕ числа TRNG RK3566 с живой платы, из вендорского Linux, через
# /dev/mem. Байты идут в стандартный вывод, всё остальное - в stderr.
#
#   python3 dump.py [сколько килобайт] > raw.bin
#
# Зачем отдельно от probe.py. probe.py отвечает на вопрос "блок вообще
# отвечает и выдаёт разное" - четырёх выборок для этого хватает. Здесь вопрос
# другой: КАКИЕ это числа. И его нельзя задать через /dev/random самой
# MEECHO: там выход пула, смешанный SHA-256, и он будет выглядеть случайным,
# даже если источник плох. Судить об источнике можно только по сырому выходу,
# а прочитать его дёшево можно там же, где снимался эталон, - под Linux.
#
# Раскладка регистров - та же, что в probe.py; номера взяты у mainline
# (drivers/char/hw_random/rockchip-rng.c), не код.

import mmap
import os
import struct
import sys
import time

BASE = 0xfe388000
SIZE = 0x1000

CTL = 0x0400
SAMPLE_CNT = 0x0404
DOUT = 0x0410

CTL_START = 1 << 0
CTL_ENABLE = 1 << 1
CTL_LEN_256 = 0x3 << 4

CHUNK = 32          # столько отдаёт блок за один запуск
SAMPLE_PERIOD = 1000  # как у mainline


def hiword(value, mask):
    """Запись с маской: старшие 16 бит говорят, какие младшие менять."""
    return (mask << 16) | (value & 0xFFFF)


def main():
    want_kb = int(sys.argv[1]) if len(sys.argv) > 1 else 64
    want = want_kb * 1024

    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    m = mmap.mmap(fd, SIZE, offset=BASE)

    def rd(off):
        return struct.unpack("<I", m[off:off + 4])[0]

    def wr(off, val):
        m[off:off + 4] = struct.pack("<I", val & 0xFFFFFFFF)

    out = sys.stdout.buffer
    got = 0
    timeouts = 0
    repeats = 0
    prev = None
    t0 = time.time()

    while got < want:
        wr(SAMPLE_CNT, SAMPLE_PERIOD)
        wr(CTL, hiword(CTL_LEN_256 | CTL_ENABLE | CTL_START,
                       CTL_LEN_256 | CTL_ENABLE | CTL_START))

        deadline = time.time() + 0.05
        ok = False
        while time.time() < deadline:
            if (rd(CTL) & CTL_START) == 0:
                ok = True
                break

        data = bytes(m[DOUT:DOUT + CHUNK])
        wr(CTL, hiword(0, CTL_LEN_256 | CTL_ENABLE | CTL_START))

        if not ok:
            timeouts += 1
            # Не дождались - число недостоверно, в поток его не пускаем.
            continue
        # Повтор подряд - тоже находка, а не повод молчать.
        if data == prev:
            repeats += 1
        prev = data

        out.write(data)
        got += len(data)

    out.flush()
    m.close()
    os.close(fd)

    dt = time.time() - t0
    sys.stderr.write(
        "снято %d байт за %.1f с (%.0f байт/с), таймаутов %d, "
        "повторов подряд %d\n" % (got, dt, got / dt if dt else 0,
                                  timeouts, repeats))


if __name__ == "__main__":
    sys.exit(main())
