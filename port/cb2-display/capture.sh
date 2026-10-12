#!/bin/sh
# Снимок дисплейного конвейера CB2 (VOP2 -> dw-hdmi -> PHY) из работающего
# вендорского Linux (6.1.115-btt-rockchip64) - эталон для драйвера MEECHO.
#
# Всё только читается: отладочные файлы вендорских драйверов (vop2 и
# dw-hdmi печатают свои регистры сами, PHY - через собственный I2C-мастер
# контроллера, тем же путём, что пишет его драйвер) и /dev/mem для GRF и CRU
# (python3, mmap только на чтение). Запускать на плате под root:
#
#	sh capture.sh > snapshot.txt
#
# Режим, при котором снимать, задаётся снаружи: монитор в разъёме или
# "echo on > /sys/class/drm/card0-HDMI-A-1/status" (разъём принудительно
# подключён; без EDID драйвер берёт режимы по умолчанию).
D=/sys/kernel/debug
mount | grep -q debugfs || mount -t debugfs none $D

sec() { echo; echo "######## $*"; }

sec uname; uname -a
sec cmdline; cat /proc/cmdline
for c in /sys/class/drm/card0-*; do
	sec "$c"
	echo "status:  $(cat $c/status)"
	echo "enabled: $(cat $c/enabled)"
	echo "dpms:    $(cat $c/dpms 2>/dev/null)"
	echo "modes:"; cat $c/modes
	if [ -s $c/edid ]; then echo "edid:"; od -An -tx1 -v $c/edid; fi
done
sec fb; cat /proc/fb
for f in /sys/class/graphics/fb*; do
	[ -e $f/virtual_size ] || continue
	echo "$f: size $(cat $f/virtual_size) bpp $(cat $f/bits_per_pixel) stride $(cat $f/stride) mode $(cat $f/modes 2>/dev/null)"
done
sec "dri summary"; cat $D/dri/0/summary
sec "dri state"; cat $D/dri/0/state
sec "dri framebuffer"; cat $D/dri/0/framebuffer
sec "vop2 active_regs"; cat $D/dri/0/active_regs
sec "vop2 regs"; cat $D/dri/0/regs
sec "dw-hdmi status"; cat $D/dw-hdmi/status
sec "dw-hdmi ctrl"; cat $D/dw-hdmi/ctrl
sec "dw-hdmi phy"; cat $D/dw-hdmi/phy
sec "clk_summary (display)"
grep -iE "vop|hdmi|vpll|hpll|gpll |cpll |dclk_vop|aclk_vo|pclk_vo|hclk_vo|clk_summary|enable_cnt" \
	$D/clk/clk_summary
sec "pm_genpd"; grep -A3 -E "^vo " $D/pm_genpd/pm_genpd_summary
sec "interrupts"; grep -iE "vop|hdmi" /proc/interrupts
sec "GRF/CRU/PMU via /dev/mem"
python3 - <<'EOF'
import mmap, os, struct
def block(base, size):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    os.close(fd)
    return m
def dump(name, base, ranges):
    m = block(base, 0x1000 if base & 0xfff == 0 else 0x1000)
    print("# %s @ 0x%08x" % (name, base))
    for lo, hi in ranges:
        for off in range(lo, hi, 16):
            w = [struct.unpack_from("<I", m, off + 4 * i)[0] for i in range(4)]
            print("  0x%04x: %s" % (off, " ".join("%08x" % x for x in w)))
    m.close()
# GRF: SYS_GRF; VO_CON0..1 (0x360..0x364), IOMUX GPIO4 (HDMI DDC/CEC/HPD), SOC_CON
dump("SYS_GRF", 0xfdc60000, [(0x0060, 0x0080), (0x0300, 0x0320), (0x0360, 0x0370), (0x0500, 0x0520)])
# CRU: PLL (HPLL/VPLL), CLKSEL CON35..CON41 (VOP/VO), CLKGATE 0x330..0x350, SOFTRST
dump("CRU", 0xfdd20000, [(0x0000, 0x00e0), (0x0100, 0x0240), (0x0300, 0x0380), (0x0400, 0x0480)])
# PMU power domains (PMU_PWR_DWN_ST etc.)
dump("PMU", 0xfdd90000, [(0x0000, 0x0100)])
# PMUCRU (HPLL lives here on RK3568)
dump("PMUCRU", 0xfdd00000, [(0x0000, 0x00c0), (0x0100, 0x0140), (0x0180, 0x01a0)])
EOF
