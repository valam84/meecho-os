#!/usr/bin/env python3
# Снимок контроллера SD-карты CB2 (dw-mshc @ 0xfe2b0000) и его тактов в CRU -
# из работающего вендорского Linux, через /dev/mem, только чтением.
#
# Эталон для sdmmc_dwmshc.c: что стоит в регистрах у системы, которая с этой
# картой живёт. Регистр данных (FIFO, 0x200) не читается - чтение из FIFO
# забирает слово у идущего обмена. Всё остальное в этом диапазоне читается
# без побочных действий: у dw-mshc нет регистров, сбрасываемых чтением
# (RINTSTS гасится записью единиц).
import mmap, os, struct, sys

def block(base, size):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    os.close(fd)
    return m

def rd(m, off):
    return struct.unpack_from("<I", m, off)[0]

DWMSHC = [
    (0x00, "CTRL"), (0x04, "PWREN"), (0x08, "CLKDIV"), (0x0c, "CLKSRC"),
    (0x10, "CLKENA"), (0x14, "TMOUT"), (0x18, "CTYPE"), (0x1c, "BLKSIZ"),
    (0x20, "BYTCNT"), (0x24, "INTMASK"), (0x28, "CMDARG"), (0x2c, "CMD"),
    (0x30, "RESP0"), (0x34, "RESP1"), (0x38, "RESP2"), (0x3c, "RESP3"),
    (0x40, "MINTSTS"), (0x44, "RINTSTS"), (0x48, "STATUS"), (0x4c, "FIFOTH"),
    (0x50, "CDETECT"), (0x54, "WRTPRT"), (0x58, "GPIO"), (0x5c, "TCBCNT"),
    (0x60, "TBBCNT"), (0x64, "DEBNCE"), (0x68, "USRID"), (0x6c, "VERID"),
    (0x70, "HCON"), (0x74, "UHS_REG"), (0x78, "RST_N"), (0x80, "BMOD"),
    (0x84, "PLDMND"), (0x88, "DBADDR"), (0x8c, "IDSTS"), (0x90, "IDINTEN"),
    (0x94, "DSCADDR"), (0x98, "BUFADDR"), (0x100, "CARDTHRCTL"),
    (0x104, "BACK_END_POWER"), (0x108, "UHS_REG_EXT"), (0x10c, "EMMC_DDR"),
    (0x110, "ENABLE_SHIFT"),
]

CRU = [
    (0x170, "CLKSEL_CON28 (cclk_emmc 14:12)"),
    (0x178, "CLKSEL_CON30 (clk_sdmmc0 10:8, sdmmc1 14:12)"),
    (0x324, "CLKGATE_CON9 (emmc)"),
    (0x33c, "CLKGATE_CON15 (hclk_sdmmc0 0, clk_sdmmc0 1)"),
    (0x434, "SOFTRST_CON13 (SRST_SDMMC0 = 212 -> bit 4)"),
    (0x580, "SDMMC0_CON0 (drive phase)"), (0x584, "SDMMC0_CON1 (sample phase)"),
    (0x588, "SDMMC1_CON0"), (0x58c, "SDMMC1_CON1"),
    (0x598, "EMMC_CON0"), (0x59c, "EMMC_CON1"),
]

for base, name in ((0xfe2b0000, "mmc@fe2b0000 (SD)"),
                   (0xfe2c0000, "mmc@fe2c0000 (SDIO)")):
    m = block(base, 0x1000)
    print("# %s" % name)
    for off, n in DWMSHC:
        print("  %-15s 0x%03x = 0x%08x" % (n, off, rd(m, off)))
    m.close()

m = block(0xfdd20000, 0x1000)
print("# CRU @ 0xfdd20000")
for off, n in CRU:
    print("  0x%03x = 0x%08x  %s" % (off, rd(m, off), n))
m.close()
