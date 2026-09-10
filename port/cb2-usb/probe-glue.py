#!/usr/bin/env python3
# Снимок ОБВЯЗКИ USB RK3566 с живой платы: то, что стоит между шиной и
# контроллером и без чего регистры xHCI не значат ничего. Только чтение.
#
# Зачем отдельный файл. probe.py снимает сами контроллеры и аналоговую часть
# PHY по адресу узла (fe8a0000). Но настройка PHY, которую делает драйвер,
# пишется НЕ туда: у узла есть ссылка rockchip,usbgrf на отдельный syscon
# (fdca0000), и все поля вида phy_sus, clkout_ctl, ls_filter_con, детекторы и
# состояние живут в нём. Два разных файла регистров с похожими смещениями —
# ровно тот случай, когда ошибка молчит: запись уходит в существующий регистр
# не того блока.
#
# Что здесь снимается и почему именно это:
#
#   usb2phy0_grf  0xfdca0000  настройка PHY0 (оба xHCI висят на нём)
#   usb2phy1_grf  0xfdca8000  настройка PHY1 (четыре EHCI/OHCI)
#   phy0 analog   0xfe8a0000  0x000..0x0ff и 0x400..0x4ff: два порта
#   CRU           0xfdd20000  такты и сбросы контроллера и PHY, мультиплексор
#                             USB480M в MODE_CON0 — 480 МГц для USB2 даёт PHY,
#                             и CRU должен смотреть на него, а не на xin24m
#   PMUCRU        0xfdd00000  опора 24 МГц самого PHY живёт здесь, не в CRU
#   PMU           0xfdd90000  домен питания PD_PIPE: оба xHCI внутри него,
#                             и выключенный домен читается как шина без ответа
#
# Раскладка — из исходников вендорского ядра той же версии, что на плате
# (6.1.115, armbian/linux-rockchip rk-6.1-rkr5.1): clk.h, clk-rk3568.c,
# pm_domains.c, phy-rockchip-inno-usb2.c. Взяты номера регистров и разрядность
# полей, то есть факты о железе.

import mmap, os, struct, sys

CRU = 0xFDD20000
PMUCRU = 0xFDD00000
PMU = 0xFDD90000

# CRU: CLKSEL_CON(x) = 0x100 + 4x, CLKGATE_CON(x) = 0x300 + 4x,
#      SOFTRST_CON(x) = 0x400 + 4x, MODE_CON0 = 0xc0.
def clksel(x):
    return 0x100 + x * 4


def clkgate(x):
    return 0x300 + x * 4


def softrst(x):
    return 0x400 + x * 4


CRU_REGS = [
    (0x00C0, "MODE_CON0", "биты 15:14 — источник usb480m: 0 xin24m, "
                          "1 usb480m_phy, 2 rtc32k"),
    (clksel(29), "CLKSEL_CON29", "биты 1:0 aclk_pipe mux, 7:4 pclk_pipe div, "
                                 "бит 8 usb3otg0_suspend mux"),
    (clksel(32), "CLKSEL_CON32", "aclk_usb/hclk_usb/pclk_usb — блок EHCI/OHCI"),
    (clkgate(10), "CLKGATE_CON10", "бит 0 aclk_pipe, 1 pclk_pipe, "
                                   "8 aclk_usb3otg0, 9 clk_usb3otg0_ref, "
                                   "10 clk_usb3otg0_suspend, 12..14 otg1"),
    (clkgate(16), "CLKGATE_CON16", "0 aclk_usb, 1 hclk_usb, 2 pclk_usb, "
                                   "12.. hclk_usb2host*"),
    (softrst(9), "SOFTRST_CON09", "линия 148 = SRST_USB3OTG0 -> бит 4; "
                                  "149 = USB3OTG1 -> бит 5"),
    (softrst(28), "SOFTRST_CON28", "линии 448..463: 458 = p_usb2phy0_grf "
                                   "-> бит 10, 459 = p_usb2phy1_grf -> 11"),
    (softrst(29), "SOFTRST_CON29", "линии 464..479: 464 usb2phy0_por -> бит 0, "
                                   "465 usb2phy0_usb3otg0 -> 1, "
                                   "466 usb2phy0_usb3otg1 -> 2, "
                                   "467 usb2phy1_por -> 3"),
]

PMUCRU_REGS = [
    (0x0100, "PMU_CLKSEL_CON8", "бит 0 — источник clk_usbphy0_ref: "
                                "0 clk_ref24m, 1 xin_osc0_usbphy0_g; бит 1 — "
                                "то же для phy1"),
    (0x0188, "PMU_CLKGATE_CON2", "бит 0 clk_ref24m, 1 xin_osc0_usbphy0_g, "
                                 "2 xin_osc0_usbphy1_g"),
]

PMU_REGS = [
    (0x0050, "PMU_PWRDN_ST/req", "req_offset: PD_PIPE — бит 11"),
    (0x0060, "PMU_ACK", "ack_offset: PD_PIPE — бит 11"),
    (0x0068, "PMU_IDLE", "idle_offset: PD_PIPE — бит 11"),
    (0x0098, "PMU_PWR_STATUS", "status_offset: PD_PIPE — бит 8, "
                               "1 = домен ВЫКЛЮЧЕН"),
    (0x00A0, "PMU_PWR_CON", "pwr_offset: PD_PIPE — бит 8, hiword-masked"),
]

# Поля usbgrf, по которым драйвер поднимает PHY (вендорский rk3568_phy_cfgs).
GRF_REGS = [
    (0x0000, "OTG_CON0", "биты 8:0 phy_sus (0 = порт работает, 0x1d1 = "
                         "выключен), бит 9 iddig_en, 10 iddig_output"),
    (0x0004, "HOST_CON0", "биты 8:0 phy_sus (0x1d2 = управление от "
                          "контроллера)"),
    (0x0008, "CON2", "бит 4 clkout_ctl (0 = 480 МГц наружу включены), "
                     "биты 15:14 bvalid_grf_sel, 2 bypass_dm_en, "
                     "3 bypass_sel, 7..12 детектор зарядки"),
    (0x000C, "CON3", "вендор пишет 0x80008000 — пробуждение host-порта"),
    (0x0040, "LS_FILTER_CON", "биты 19:0, вендор ставит 0x30100"),
    (0x0048, "BVALID_FILTER", "10 мс при pclk 100 МГц"),
    (0x004C, "ID_FILTER", "10 мс при pclk 100 МГц"),
    (0x0080, "DET_EN", "разрешение детекторов: бит 0 ls otg, 1 ls host, "
                       "2 bvalid, 4 idrise, 5 idfall"),
    (0x0084, "DET_ST", "состояние тех же детекторов"),
    (0x0088, "DET_CLR", "сброс тех же детекторов"),
    (0x00C0, "STATUS", "биты 5:4 utmi_ls otg, 6 utmi_iddig, 9 bvalid, "
                       "10 avalid, 17:16 utmi_ls host, 19 utmi_hstdet"),
]


def opener(base, size=0x1000):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, size, offset=base, prot=mmap.PROT_READ)

    def rd(off):
        return struct.unpack("<I", m[off:off + 4])[0]

    return fd, m, rd


def show(title, base, regs, size=0x1000):
    print("=" * 72)
    print("%s  (0x%08x)" % (title, base))
    print("=" * 72)
    fd, m, rd = opener(base, size)
    for off, name, note in regs:
        print("  %-16s +0x%04x = 0x%08x   %s" % (name, off, rd(off), note))
    m.close()
    os.close(fd)
    print()


def dump(title, base, ranges, size=0x1000):
    print("=" * 72)
    print("%s  (0x%08x)" % (title, base))
    print("=" * 72)
    fd, m, rd = opener(base, size)
    for start, count in ranges:
        for i in range(0, count, 4):
            vals = " ".join("%08x" % rd(start + (i + j) * 4) for j in range(4))
            print("  +0x%04x  %s" % (start + i * 4, vals))
        print()
    m.close()
    os.close(fd)


if __name__ == "__main__":
    if not os.path.exists("/dev/mem"):
        sys.exit("нет /dev/mem: запускать на плате, из вендорского Linux")

    show("usb2phy0_grf — настройка PHY0, оба xHCI на нём", 0xFDCA0000, GRF_REGS)
    show("usb2phy1_grf — настройка PHY1, четыре EHCI/OHCI", 0xFDCA8000,
         GRF_REGS)
    show("CRU — такты и сбросы", CRU, CRU_REGS)
    show("PMUCRU — опора PHY", PMUCRU, PMUCRU_REGS)
    show("PMU — домен питания PD_PIPE", PMU, PMU_REGS)

    # Аналоговая часть PHY0 целиком: порт OTG с 0x000, порт HOST с 0x400.
    # Вендорское «подстраивание» пишет именно сюда (предыскажение, высота
    # глаза, приёмник дифференциального сигнала), и это единственные числа,
    # которых нет ни в дереве, ни в спецификации USB.
    dump("PHY0 analog 0xfe8a0000: OTG-порт 0x000, HOST-порт 0x400",
         0xFE8A0000, [(0x000, 64), (0x400, 64)], size=0x10000)
