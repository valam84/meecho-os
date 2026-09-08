#!/usr/bin/env python3
# Проба TRNG RK3566 с ЖИВОЙ платы, из вендорского Linux, через /dev/mem.
#
# Зачем: узел rng@fe388000 в вендорском дереве стоит status = "disabled", то
# есть работающего эталона на этом железе нет ни у кого. Прежде чем писать
# драйвер для MEECHO вслепую - а именно так был потерян день на PHY, - надо
# убедиться, что блок вообще отвечает и выдаёт разные числа. Здесь это
# делается там, где ошибиться дёшево: под Linux, который можно перезагрузить.
#
# Раскладка взята у mainline (drivers/char/hw_random/rockchip-rng.c,
# rockchip,rk3568-rng) - только номера регистров, не код.
#
#   CTL         base + 0x0400   бит 0 START, бит 1 ENABLE, 0x3<<4 длина 256 бит
#   SAMPLE_CNT  base + 0x0404
#   DOUT        base + 0x0410   32 байта
#
# Регистры такие же hiword-masked, как у CRU: старшая половина - маска записи.

import mmap, os, struct, sys, time

BASE = 0xfe388000
SIZE = 0x1000

CTL = 0x0400
SAMPLE_CNT = 0x0404
DOUT = 0x0410

CTL_START = 1 << 0
CTL_ENABLE = 1 << 1
CTL_LEN_256 = 0x3 << 4


def hiword(value, mask):
    """Запись с маской: старшие 16 бит говорят, какие младшие менять."""
    return (mask << 16) | (value & 0xFFFF)


def main():
    fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    m = mmap.mmap(fd, SIZE, offset=BASE)

    def rd(off):
        return struct.unpack("<I", m[off:off + 4])[0]

    def wr(off, val):
        m[off:off + 4] = struct.pack("<I", val & 0xFFFFFFFF)

    print("CTL        = 0x%08x" % rd(CTL))
    print("SAMPLE_CNT = 0x%08x" % rd(SAMPLE_CNT))
    print("DOUT[0]    = 0x%08x" % rd(DOUT))

    samples = []
    for attempt in range(4):
        # период выборки, как у mainline
        wr(SAMPLE_CNT, 1000)
        # длина 256 бит + включить кольцо генератора + запустить
        wr(CTL, hiword(CTL_LEN_256 | CTL_ENABLE | CTL_START,
                       CTL_LEN_256 | CTL_ENABLE | CTL_START))

        deadline = time.time() + 0.05
        ok = False
        while time.time() < deadline:
            if (rd(CTL) & CTL_START) == 0:
                ok = True
                break
        ctl_after = rd(CTL)
        data = bytes(m[DOUT:DOUT + 32])
        # выключить кольцо
        wr(CTL, hiword(0, CTL_LEN_256 | CTL_ENABLE | CTL_START))

        print("попытка %d: START снят=%s  CTL=0x%08x" % (attempt, ok, ctl_after))
        print("   " + data.hex())
        samples.append(data)

    m.close()
    os.close(fd)

    print()
    uniq = len(set(samples))
    allzero = all(s == b"\0" * 32 for s in samples)
    allff = all(s == b"\xff" * 32 for s in samples)
    print("разных значений: %d из %d" % (uniq, len(samples)))
    print("все нули: %s   все FF: %s" % (allzero, allff))
    if uniq == len(samples) and not allzero and not allff:
        print("ВЫВОД: блок отвечает и выдаёт разное - драйвер имеет смысл")
    else:
        print("ВЫВОД: блок НЕ даёт годных чисел - драйвер писать не по чему")


if __name__ == "__main__":
    sys.exit(main())
