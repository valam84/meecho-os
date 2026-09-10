#!/usr/bin/env python3
# Снимок контроллеров USB RK3566 с ЖИВОЙ платы, из вендорского Linux, через
# /dev/mem. Только чтение: контроллером в этот момент пользуется работающий
# Linux, и запись в него сломала бы и эталон, и систему, из которой он снят.
#
# Зачем: тот же порядок, что дважды окупился на GMAC и TRNG - эталон снимается
# с железа ДО первой строки драйвера. Здесь нас интересует не "работает ли"
# (работает, Linux на нём держит хаб), а числа, которых нет ни в дереве, ни в
# спецификации: версия ядра xHCI, число слотов и портов, разрядность шины,
# карта расширенных возможностей (какой порт какого протокола) и состояние
# глюй-слоя DesignWare, который стоит ПЕРЕД xHCI и без которого регистры xHCI
# ничего не значат.
#
# Раскладка: xHCI 1.2 (спецификация Intel, открытая) и DWC3 (глобальные
# регистры с base + 0xC100, как во всех драйверах DWC3) - только номера
# регистров.
#
#   usbdrd  usb@fcc00000  snps,dwc3, phy: usb2-phy только      -> bus 1 (480)
#   usbhost usb@fd000000  snps,dwc3, phy: usb2-phy + usb3-phy  -> bus 6,7 (5000)
#
# Запуск на плате: python3 probe.py [> reference.txt]

import mmap, os, struct, sys

CONTROLLERS = [
    ("usbdrd  usb@fcc00000", 0xFCC00000),
    ("usbhost usb@FD000000", 0xFD000000),
]

PHYS = [
    ("usb2phy0 usb2-phy@fe8a0000", 0xFE8A0000),
    ("usb2phy1 usb2-phy@fe8b0000", 0xFE8B0000),
]

MAPSIZE = 0x10000

# --- DWC3 глобальные регистры -------------------------------------------
DWC3 = [
    (0xC100, "GSBUSCFG0"),
    (0xC104, "GSBUSCFG1"),
    (0xC110, "GCTL"),
    (0xC118, "GSTS"),
    (0xC120, "GSNPSID"),
    (0xC124, "GGPIO"),
    (0xC128, "GUID"),
    (0xC12C, "GUCTL"),
    (0xC130, "GBUSERRADDRLO"),
    (0xC140, "GHWPARAMS0"),
    (0xC144, "GHWPARAMS1"),
    (0xC148, "GHWPARAMS2"),
    (0xC14C, "GHWPARAMS3"),
    (0xC150, "GHWPARAMS4"),
    (0xC154, "GHWPARAMS5"),
    (0xC158, "GHWPARAMS6"),
    (0xC15C, "GHWPARAMS7"),
    (0xC200, "GUSB2PHYCFG0"),
    (0xC2C0, "GUSB3PIPECTL0"),
    (0xC600, "GFLADJ"),
    (0xC704, "DCFG"),
    (0xC70C, "DSTS"),
]

# --- Регистры возможностей xHCI -----------------------------------------
CAP = [
    (0x00, "CAPLENGTH/HCIVERSION"),
    (0x04, "HCSPARAMS1"),
    (0x08, "HCSPARAMS2"),
    (0x0C, "HCSPARAMS3"),
    (0x10, "HCCPARAMS1"),
    (0x14, "DBOFF"),
    (0x18, "RTSOFF"),
    (0x1C, "HCCPARAMS2"),
]


def dump_range(rd, base, count, label):
    print("  %s:" % label)
    for i in range(0, count, 4):
        vals = " ".join("%08x" % rd(base + (i + j) * 4) for j in range(4))
        print("    +0x%04x  %s" % (base + i * 4, vals))


def probe_controller(name, base):
    print("=" * 72)
    print("%s  (base 0x%08x)" % (name, base))
    print("=" * 72)

    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, MAPSIZE, offset=base, prot=mmap.PROT_READ)

    def rd(off):
        return struct.unpack("<I", m[off:off + 4])[0]

    print("--- DWC3 (глюй-слой перед xHCI) ---")
    for off, nm in DWC3:
        print("  %-14s +0x%04x = 0x%08x" % (nm, off, rd(off)))

    snpsid = rd(0xC120)
    print("  -> GSNPSID 0x%08x: ядро %s, версия %x.%02x"
          % (snpsid, "DWC3" if (snpsid >> 16) == 0x5533 else "?",
             (snpsid >> 12) & 0xF, snpsid & 0xFFF))

    print("--- xHCI capability ---")
    for off, nm in CAP:
        print("  %-22s +0x%02x = 0x%08x" % (nm, off, rd(off)))

    caplen = rd(0x00) & 0xFF
    hciver = (rd(0x00) >> 16) & 0xFFFF
    hcs1 = rd(0x04)
    hcs2 = rd(0x08)
    hcc1 = rd(0x10)
    dboff = rd(0x14) & ~0x3
    rtsoff = rd(0x18) & ~0x1F

    maxslots = hcs1 & 0xFF
    maxintrs = (hcs1 >> 8) & 0x7FF
    maxports = (hcs1 >> 24) & 0xFF
    ist = hcs2 & 0xF
    erst_max = (hcs2 >> 4) & 0xF
    spr = (hcs2 >> 26) & 1
    spb = ((hcs2 >> 27) & 0x1F) | (((hcs2 >> 21) & 0x1F) << 5)

    print("  -> xHCI %x.%x, CAPLENGTH %d" % (hciver >> 8, (hciver >> 4) & 0xF,
                                             caplen))
    print("  -> слотов %d, портов %d, векторов прерываний %d"
          % (maxslots, maxports, maxintrs))
    print("  -> IST %d, ERST max 2^%d, scratchpad %d (restore %d)"
          % (ist, erst_max, spb, spr))
    print("  -> AC64 %d (64-битная шина), CSZ %d (контекст 64 байта), "
          "PPC %d, PAE %d, MaxPSASize %d, xECP +0x%x"
          % (hcc1 & 1, (hcc1 >> 2) & 1, (hcc1 >> 3) & 1, (hcc1 >> 8) & 1,
             (hcc1 >> 12) & 0xF, ((hcc1 >> 16) & 0xFFFF) * 4))
    print("  -> DBOFF +0x%x, RTSOFF +0x%x" % (dboff, rtsoff))

    print("--- xHCI operational (+0x%02x) ---" % caplen)
    for off, nm in [(0x00, "USBCMD"), (0x04, "USBSTS"), (0x08, "PAGESIZE"),
                    (0x14, "DNCTRL"), (0x18, "CRCR_LO"), (0x1C, "CRCR_HI"),
                    (0x30, "DCBAAP_LO"), (0x34, "DCBAAP_HI"),
                    (0x38, "CONFIG")]:
        print("  %-10s +0x%03x = 0x%08x" % (nm, caplen + off, rd(caplen + off)))

    for port in range(maxports):
        p = caplen + 0x400 + port * 0x10
        portsc = rd(p)
        print("  PORTSC%d   +0x%03x = 0x%08x  CCS %d PED %d PR %d PLS %d "
              "PP %d speed %d"
              % (port + 1, p, portsc, portsc & 1, (portsc >> 1) & 1,
                 (portsc >> 4) & 1, (portsc >> 5) & 0xF, (portsc >> 9) & 1,
                 (portsc >> 10) & 0xF))

    # Расширенные возможности: карта "порт -> протокол" живёт только здесь.
    xecp = ((rd(0x10) >> 16) & 0xFFFF) * 4
    print("--- xHCI extended capabilities ---")
    off = xecp
    guard = 0
    while off and guard < 32:
        guard += 1
        val = rd(off)
        cap_id = val & 0xFF
        nxt = ((val >> 8) & 0xFF) * 4
        if cap_id == 2:
            name = struct.pack("<I", rd(off + 4)).decode("latin1")
            ports = rd(off + 8)
            print("  +0x%04x  id 2 Supported Protocol '%s' %d.%02d, "
                  "порты %d..%d, скоростей %d"
                  % (off, name, (val >> 24) & 0xFF, (val >> 16) & 0xFF,
                     ports & 0xFF, (ports & 0xFF) + ((ports >> 8) & 0xFF) - 1,
                     (ports >> 28) & 0xF))
            for i in range((ports >> 28) & 0xF):
                psi = rd(off + 0x10 + i * 4)
                mant = (psi >> 16) & 0xFFFF
                exp = (psi >> 4) & 0x3
                unit = {0: "b/s", 1: "Kb/s", 2: "Mb/s", 3: "Gb/s"}[exp]
                print("      PSI %d: %d %s (psiv %d)"
                      % (i, mant, unit, psi & 0xF))
        else:
            print("  +0x%04x  id %d = 0x%08x" % (off, cap_id, val))
        if nxt == 0:
            break
        off += nxt

    m.close()
    os.close(fd)
    print()


def probe_phy(name, base):
    print("=" * 72)
    print("%s  (base 0x%08x)" % (name, base))
    print("=" * 72)
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, 0x1000, offset=base, prot=mmap.PROT_READ)

    def rd(off):
        return struct.unpack("<I", m[off:off + 4])[0]

    # У rk3568 usb2phy - это отдельный GRF: смещения 0x0000..0x00ff несут
    # настройку двух портов (host и otg), выше - состояние и прерывания.
    dump_range(rd, 0x00, 32, "0x0000..0x007f")
    dump_range(rd, 0x100, 32, "0x0100..0x017f")
    m.close()
    os.close(fd)
    print()


if __name__ == "__main__":
    if not os.path.exists("/dev/mem"):
        sys.exit("нет /dev/mem: запускать на плате, из вендорского Linux")
    for name, base in CONTROLLERS:
        probe_controller(name, base)
    for name, base in PHYS:
        probe_phy(name, base)
