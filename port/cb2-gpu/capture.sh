#!/bin/sh
# Снимок ускорителя Mali-G52 CB2 (gpu@fde60000) из работающего вендорского
# Linux (6.1.115-btt-rockchip64) - эталон для службы mali в MEECHO.
#
# Что снимается: какой драйвер владеет узлом (bifrost_kbase или panfrost),
# его отчёт о себе, узел дерева, такты, домен питания, напряжение vdd_gpu,
# прерывания, и регистры CRU, PMU и самого ускорителя через /dev/mem
# (python3, mmap только на чтение).
#
# Регистры ускорителя читаются, только если PMU говорит, что домен PD_GPU
# включён и не в простое, а CRU - что такты блока открыты: обращение к
# обесточенному или незатактированному блоку на Rockchip вешает шину, и
# плату тогда вернёт только сторож или человек у выключателя.
#
# Запуск на плате под root:
#
#	sh capture.sh      > idle.txt    как есть: драйвер мог погасить блок
#	sh capture.sh on   > on.txt      сначала попросить драйвер держать
#	                                 блок включённым, после снимка вернуть
#
# "on" - единственная запись: power_policy у kbase (always_on) или
# power/control у panfrost (on). Прежнее значение возвращается в конце.
MODE=${1:-idle}
D=/sys/kernel/debug
mount | grep -q " $D " || mount -t debugfs none $D 2>/dev/null
NODE=/proc/device-tree/gpu@fde60000
DEV=/sys/bus/platform/devices/fde60000.gpu

sec() { echo; echo "######## $*"; }

sec uname; uname -a
sec cmdline; cat /proc/cmdline
sec date; date

sec "driver"
if [ -e $DEV/driver ]; then
	DRV=$(basename "$(readlink $DEV/driver)")
else
	DRV=none
fi
echo "bound driver: $DRV"
ls -l $DEV/ 2>&1
sec "modules"; lsmod 2>/dev/null | grep -iE "mali|kbase|panfrost" || echo "(none as modules)"
sec "kernel config"
if [ -r /proc/config.gz ]; then
	zcat /proc/config.gz | grep -E "MALI|PANFROST|BIFROST|DRM_PANTHOR"
elif [ -r /boot/config-$(uname -r) ]; then
	grep -E "MALI|PANFROST|BIFROST|DRM_PANTHOR" /boot/config-$(uname -r)
else
	echo "(no config)"
fi
sec "dmesg (gpu)"
dmesg | grep -iE "mali|kbase|panfrost|bifrost|fde60000|gpu" | head -120

sec "device tree $NODE"
for p in $NODE/*; do
	[ -f "$p" ] || continue
	n=$(basename $p)
	case $n in
	compatible|interrupt-names|clock-names|status|name)
		printf '%-24s %s\n' "$n" "$(tr '\0' ' ' < $p)" ;;
	*)
		printf '%-24s %s\n' "$n" "$(od -An -tx4 --endian=big -v $p | tr -s ' \n' ' ')" ;;
	esac
done
for c in $NODE/*/; do
	[ -d "$c" ] && echo "subnode: $(basename $c)"
done

ORIG=
if [ "$MODE" = on ]; then
	sec "forcing the block on"
	if [ -w $DEV/power_policy ]; then
		echo "power_policy before: $(cat $DEV/power_policy)"
		ORIG=$(sed 's/.*\[\([a-z_]*\)\].*/\1/' $DEV/power_policy)
		echo always_on > $DEV/power_policy
		echo "power_policy now:    $(cat $DEV/power_policy)"
	elif [ -w $DEV/power/control ]; then
		ORIG=$(cat $DEV/power/control)
		echo "power/control before: $ORIG"
		echo on > $DEV/power/control
		echo "power/control now:    $(cat $DEV/power/control)"
	else
		echo "no knob: neither power_policy nor power/control is writable"
	fi
	sleep 1
fi

sec "driver's own report"
for f in gpuinfo core_mask power_policy dvfs_period pm_poweroff gpu_memory \
	 power/control power/runtime_status power/runtime_active_time \
	 power/runtime_suspended_time power/autosuspend_delay_ms; do
	[ -r $DEV/$f ] && printf '%-30s %s\n' "$f" "$(cat $DEV/$f 2>&1 | tr '\n' ' ')"
done
for df in /sys/class/devfreq/fde60000.gpu /sys/class/devfreq/*gpu*; do
	[ -d $df ] || continue
	echo "devfreq $df:"
	for f in governor cur_freq min_freq max_freq available_frequencies \
		 available_governors load; do
		[ -r $df/$f ] && printf '  %-24s %s\n' "$f" "$(cat $df/$f 2>&1 | tr '\n' ' ')"
	done
	[ -r $df/trans_stat ] && { echo "  trans_stat:"; sed 's/^/    /' $df/trans_stat; }
	break
done
sec "opp table (from the tree)"
for o in /proc/device-tree/opp-table-1/opp-*; do
	[ -d $o ] || continue
	hz=$(od -An -tu8 --endian=big $o/opp-hz 2>/dev/null | tr -d ' ')
	uv=$(od -An -tu4 --endian=big $o/opp-microvolt 2>/dev/null | tr -s ' \n' ' ')
	printf '  %-20s hz %-12s uV %s\n' "$(basename $o)" "$hz" "$uv"
done

sec "regulator vdd_gpu"
for r in /sys/class/regulator/regulator.*; do
	[ "$(cat $r/name 2>/dev/null)" = vdd_gpu ] || continue
	for f in name microvolts state status num_users opmode min_microvolts max_microvolts; do
		[ -r $r/$f ] && printf '  %-16s %s\n' "$f" "$(cat $r/$f 2>&1)"
	done
done
[ -r $D/regulator/regulator_summary ] && grep -iE "vdd_gpu|regulator.*use|fde60000" \
	$D/regulator/regulator_summary

sec "clk_summary (gpu)"
grep -iE "clock .*enable_cnt|---|gpu|gpll |cpll |npll |mpll |scmi" $D/clk/clk_summary
sec "pm_genpd"
grep -E "^domain|^gpu|fde60000" $D/pm_genpd/pm_genpd_summary
[ -r $D/pm_genpd/gpu/current_state ] && echo "gpu current_state: $(cat $D/pm_genpd/gpu/current_state)"
sec "interrupts"
head -1 /proc/interrupts
grep -iE "mali|gpu|job|mmu|panfrost" /proc/interrupts

sec "registers via /dev/mem"
python3 - <<'EOF'
import mmap, os, struct

def block(base, size):
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, size, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
    os.close(fd)
    return m

def rd(m, off):
    return struct.unpack_from("<I", m, off)[0]

def dump(name, base, size, ranges):
    m = block(base, size)
    print("# %s @ 0x%08x" % (name, base))
    for lo, hi in ranges:
        for off in range(lo, hi, 16):
            w = [rd(m, off + 4 * i) for i in range(4)]
            print("  0x%04x: %s" % (off, " ".join("%08x" % x for x in w)))
    return m

# PMU: power domains.  rk3568_pmu in the vendor pm_domains.c: PWR_CON 0xa0,
# PWR_STATUS 0x98, IDLE_REQ 0x50, IDLE_ST 0x68, IDLE_ACK 0x60; PD_GPU is pwr
# bit 0, req/idle/ack bit 1.  A set bit means off / idle.
pmu = dump("PMU", 0xfdd90000, 0x1000, [(0x0000, 0x0100)])
pwr_con, pwr_st = rd(pmu, 0xa0), rd(pmu, 0x98)
idle_req, idle_st, idle_ack = rd(pmu, 0x50), rd(pmu, 0x68), rd(pmu, 0x60)
pd_on = not (pwr_st & 1) and not (idle_st & 2)
print("PD_GPU: PWR_CON bit0=%d PWR_STATUS bit0=%d IDLE_REQ bit1=%d "
      "IDLE_ST bit1=%d IDLE_ACK bit1=%d -> %s" % (
      pwr_con & 1, pwr_st & 1, (idle_req >> 1) & 1, (idle_st >> 1) & 1,
      (idle_ack >> 1) & 1, "ON" if pd_on else "OFF or idle"))

# CRU: PLLs and mode, CLKSEL_CON(6) (gpu src mux 7:6, div 3:0, aclk div 9:8,
# pre mux 11, pclk div 15:12), CLKGATE_CON(2) (clk_gpu_src 0, clk_gpu 3,
# pvtm 6..9), SOFTRST_CON(2) (srst_gpu 0, a_niu 1, p_niu 2).
cru = dump("CRU", 0xfdd20000, 0x1000,
           [(0x0000, 0x00d0), (0x0100, 0x0140), (0x0300, 0x0320),
            (0x0400, 0x0420)])
sel6, gate2, rst2 = rd(cru, 0x118), rd(cru, 0x308), rd(cru, 0x408)
print("CLKSEL_CON6  = 0x%08x: gpu_src mux %d div %d, aclk div %d, "
      "pre_mux %d, pclk div %d" % (sel6, (sel6 >> 6) & 3, sel6 & 15,
      (sel6 >> 8) & 3, (sel6 >> 11) & 1, (sel6 >> 12) & 15))
print("CLKGATE_CON2 = 0x%08x: clk_gpu_src %s, clk_gpu %s" % (gate2,
      "gated" if gate2 & 1 else "open", "gated" if gate2 & 8 else "open"))
print("SOFTRST_CON2 = 0x%08x: srst_gpu %d, a_niu %d, p_niu %d" % (rst2,
      rst2 & 1, (rst2 >> 1) & 1, (rst2 >> 2) & 1))
clk_ok = not (gate2 & 1) and not (gate2 & 8) and not (rst2 & 7)

if not (pd_on and clk_ok):
    print("NOT touching the GPU block: domain %s, clocks %s" % (
          "on" if pd_on else "off", "open" if clk_ok else "gated/reset"))
    raise SystemExit(0)

# QoS of the GPU port sits behind the same domain.
dump("QOS_GPU", 0xfe128000, 0x1000, [(0x0000, 0x0020)])

gpu = block(0xfde60000, 0x4000)
regs = [
    ("GPU_ID", 0x000), ("L2_FEATURES", 0x004), ("CORE_FEATURES", 0x008),
    ("TILER_FEATURES", 0x00c), ("MEM_FEATURES", 0x010),
    ("MMU_FEATURES", 0x014), ("AS_PRESENT", 0x018), ("JS_PRESENT", 0x01c),
    ("GPU_IRQ_RAWSTAT", 0x020), ("GPU_IRQ_MASK", 0x028),
    ("GPU_IRQ_STATUS", 0x02c), ("GPU_STATUS", 0x034),
    ("LATEST_FLUSH_ID", 0x038), ("GPU_FAULTSTATUS", 0x03c),
    ("GPU_FAULTADDRESS_LO", 0x040), ("GPU_FAULTADDRESS_HI", 0x044),
    ("AFBC_FEATURES", 0x04c), ("PWR_OVERRIDE0", 0x054),
    ("PWR_OVERRIDE1", 0x058), ("CYCLE_COUNT_LO", 0x090),
    ("CYCLE_COUNT_HI", 0x094), ("TIMESTAMP_LO", 0x098),
    ("TIMESTAMP_HI", 0x09c), ("THREAD_MAX_THREADS", 0x0a0),
    ("THREAD_MAX_WORKGROUP_SIZE", 0x0a4), ("THREAD_MAX_BARRIER_SIZE", 0x0a8),
    ("THREAD_FEATURES", 0x0ac),
] + [("TEXTURE_FEATURES_%d" % n, 0x0b0 + 4 * n) for n in range(4)] \
  + [("JS_FEATURES_%d" % n, 0x0c0 + 4 * n) for n in range(16)] + [
    ("SHADER_PRESENT_LO", 0x100), ("SHADER_PRESENT_HI", 0x104),
    ("TILER_PRESENT_LO", 0x110), ("TILER_PRESENT_HI", 0x114),
    ("L2_PRESENT_LO", 0x120), ("L2_PRESENT_HI", 0x124),
    ("SHADER_READY_LO", 0x140), ("TILER_READY_LO", 0x150),
    ("L2_READY_LO", 0x160), ("SHADER_PWRTRANS_LO", 0x200),
    ("TILER_PWRTRANS_LO", 0x210), ("L2_PWRTRANS_LO", 0x220),
    ("SHADER_PWRACTIVE_LO", 0x240), ("TILER_PWRACTIVE_LO", 0x250),
    ("L2_PWRACTIVE_LO", 0x260), ("COHERENCY_FEATURES", 0x300),
    ("COHERENCY_ENABLE", 0x304), ("THREAD_TLS_ALLOC", 0x310),
    ("STACK_PRESENT_LO", 0xe00), ("STACK_READY_LO", 0xe10),
    ("JM_CONFIG", 0xf00), ("SHADER_CONFIG", 0xf04), ("TILER_CONFIG", 0xf08),
    ("L2_MMU_CONFIG", 0xf0c),
    ("JOB_IRQ_RAWSTAT", 0x1000), ("JOB_IRQ_MASK", 0x1008),
    ("JOB_IRQ_STATUS", 0x100c), ("JOB_IRQ_JS_STATE", 0x1010),
    ("JOB_IRQ_THROTTLE", 0x1014),
    ("MMU_IRQ_RAWSTAT", 0x2000), ("MMU_IRQ_MASK", 0x2008),
    ("MMU_IRQ_STATUS", 0x200c),
]
for js in range(3):
    b = 0x1800 + 0x80 * js
    regs += [("JS%d_HEAD_LO" % js, b + 0x00), ("JS%d_HEAD_HI" % js, b + 0x04),
             ("JS%d_TAIL_LO" % js, b + 0x08), ("JS%d_AFFINITY_LO" % js, b + 0x10),
             ("JS%d_CONFIG" % js, b + 0x18), ("JS%d_STATUS" % js, b + 0x24),
             ("JS%d_CONFIG_NEXT" % js, b + 0x58)]
for a in range(8):
    b = 0x2400 + 0x40 * a
    regs += [("AS%d_TRANSTAB_LO" % a, b + 0x00), ("AS%d_TRANSTAB_HI" % a, b + 0x04),
             ("AS%d_MEMATTR_LO" % a, b + 0x08), ("AS%d_MEMATTR_HI" % a, b + 0x0c),
             ("AS%d_FAULTSTATUS" % a, b + 0x1c), ("AS%d_STATUS" % a, b + 0x28),
             ("AS%d_TRANSCFG_LO" % a, b + 0x30), ("AS%d_TRANSCFG_HI" % a, b + 0x34)]
print("# GPU @ 0xfde60000")
for name, off in regs:
    print("  0x%04x %-28s 0x%08x" % (off, name, rd(gpu, off)))
gid = rd(gpu, 0)
print("GPU_ID 0x%08x: product 0x%04x (arch %d.%d rev %d, major %d), "
      "r%dp%d status %d" % (gid, gid >> 16, gid >> 28, (gid >> 24) & 15,
      (gid >> 20) & 15, (gid >> 16) & 15, (gid >> 12) & 15,
      (gid >> 4) & 255, gid & 15))
EOF

if [ -n "$ORIG" ]; then
	sec "restoring"
	if [ -w $DEV/power_policy ]; then
		echo $ORIG > $DEV/power_policy
		echo "power_policy: $(cat $DEV/power_policy)"
	else
		echo $ORIG > $DEV/power/control
		echo "power/control: $(cat $DEV/power/control)"
	fi
fi
