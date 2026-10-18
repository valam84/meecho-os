#!/usr/bin/env python3
"""The Mi Pad 4's CPU voltages, from the fuses cpuprobe reads
(port/clover/hw-cpu.md, sections 4 and 7).

    cpu-volt.py LOG        the per-corner voltages of both clusters, from
                           the "fuse row" lines of a cpu-probe.sh log, and
                           the frequencies the probe's other lines give
    cpu-volt.py --selftest the arithmetic against values worked by hand

The arithmetic is the vendor kernel's, step for step and in its order
(Xiaomi clover-o-oss: cprh-kbss-regulator.c cprh_kbss_init_regulator(),
cpr3-util.c), with the tables of its sdm660-regulator.dtsi; facts, not
code - the GPL text is not here, the numbers and the order of the steps
are.  What comes out is each corner's open-loop voltage, which the vendor
tree also makes the corner's ceiling (qcom,cpr-scaled-open-loop-voltage-
as-ceiling), and its floor; closed-loop CPR, which would go lower, is left
out on purpose - without it the open-loop voltage is the one to run at.
"""
import re
import sys

STEP = 4000                     # qcom,voltage-step
FUSE_STEP = 10000               # CPRH_KBSS_FUSE_STEP_VOLT
FUSE_BITS = 6                   # CPRH_KBSS_VOLTAGE_FUSE_SIZE, sign-magnitude
APM_THRESHOLD = 872000          # qcom,apm-threshold-voltage
APM_HYSTERESIS = 20000          # qcom,apm-hysteresis-voltage
MHZ = 1000000

CLUSTERS = {
    "silver": {                 # apc0, CPRh controller 0, VDD_APC0 (S1)
        "names": ["LowSVS", "SVS", "SVSPLUS", "NOM", "TURBO_L1"],
        "ref": [644000, 724000, 788000, 868000, 1068000],
        "fuse": [[(67, 34, 39)], [(67, 28, 33)], [(71, 3, 8)],
                 [(67, 22, 27)], [(67, 16, 21)]],
        "fmax": [2, 3, 4, 5, 8],                # 1-based corners
        "freq": {0: [300, 633.6, 902.4, 1113.6, 1401.6, 1536, 1747.2, 1843.2],
                 1: [300, 633.6, 902.4, 1113.6, 1401.6, 1536, 1747.2, 1843.2],
                 3: [300, 633.6, 902.4, 1113.6, 1401.6, 1536, 1612.8, 1843.2]},
        "ceiling": [724, 724, 724, 788, 868, 1068, 1068, 1068],
        "floor": [588, 588, 596, 652, 712, 744, 784, 844],
        "ol_adjust": [-4000, 4000, 7000, 19000, -8000],
        "range": [32, 32, 32, 40, 44, 40, 40, 40],
    },
    "gold": {                   # apc1, CPRh controller 1, VDD_APC1 (S2+S3)
        "names": ["SVS", "SVSPLUS", "NOM", "TURBO", "TURBO_L2"],
        "ref": [724000, 788000, 868000, 988000, 1068000],
        "fuse": [[(69, 17, 22)], [(69, 23, 28)], [(69, 11, 16)],
                 [(69, 5, 10)], [(70, 42, 47)]],
        "fmax": [2, 3, 4, 6, 7],
        "freq": {0: [300, 1113.6, 1401.6, 1747.2, 1958.4, 2150.4, 2457.6],
                 1: [300, 1113.6, 1401.6, 1747.2, 1958.4, 2150.4, 2208],
                 3: [300, 1113.6, 1401.6, 1747.2, 1804.8, 2150.4, 2208]},
        "ceiling": [724, 724, 788, 868, 988, 988, 1068],
        "floor": [588, 596, 652, 712, 744, 784, 844],
        "ol_adjust": [16000, 27000, 39000, 39000, 20000],
        "range": [40, 40, 40, 40, 66, 66, 40],
    },
}
SPEED_BIN = [(38, 29, 31)]
CPR_REV = [(71, 28, 30)]


def param(rows, segs):
    """cpr3_read_fuse_param(): segments of 64-bit rows, low bits first."""
    v = bits = 0
    for row, lo, hi in segs:
        n = hi - lo + 1
        v |= ((rows[row] >> lo) & ((1 << n) - 1)) << bits
        bits += n
    return v


def round_up(v, d=STEP):
    return -(-v // d) * d


def interpolate(x1, y1, x2, y2, x):
    """cpr3_interpolate(), in integers as the kernel does it."""
    if x1 >= x2 or y1 > y2 or x1 > x or x > x2:
        return y2
    return y2 - (x2 - x) * (y2 - y1) // (x2 - x1)


def corners(name, rows):
    c = CLUSTERS[name]
    nfc = len(c["ref"])
    speed_bin = param(rows, SPEED_BIN)
    if speed_bin not in c["freq"]:
        raise ValueError("speed bin %d: the vendor tree has no corners for "
                         "it" % speed_bin)
    freq = [int(round(f * MHZ)) for f in c["freq"][speed_bin]]
    n = len(freq)
    fuse_corner = []
    lo = 0
    for fc, top in enumerate(c["fmax"]):
        fuse_corner += [fc] * (top - lo)
        lo = top
    assert len(fuse_corner) == n

    # cprh_kbss_calculate_open_loop_voltages()
    raw = [param(rows, s) for s in c["fuse"]]
    fv = []
    for i in range(nfc):
        sign = -1 if raw[i] & (1 << (FUSE_BITS - 1)) else 1
        steps = raw[i] & ((1 << (FUSE_BITS - 1)) - 1)
        fv.append(c["ref"][i] + sign * steps * FUSE_STEP)
    fused = list(fv)
    fv = [v + a for v, a in zip(fv, c["ol_adjust"])]
    for i in range(1, nfc):
        fv[i] = max(fv[i], fv[i - 1])
    fmax = [c["fmax"][i] - 1 for i in range(nfc)]
    ol = [0] * n
    for i in range(fmax[0] + 1):
        ol[i] = fv[0]
    for i in range(1, nfc):
        for j in range(fmax[i - 1] + 1, fmax[i] + 1):
            ol[j] = interpolate(freq[fmax[i - 1]], fv[i - 1],
                                freq[fmax[i]], fv[i], freq[j])
    floor = [v * 1000 for v in c["floor"]]
    ceil = [v * 1000 for v in c["ceiling"]]
    # cpr3_limit_open_loop_voltages()
    for i in range(n):
        ol[i] = min(max(round_up(ol[i]), floor[i]), ceil[i])
    # cprh_adjust_voltages_for_apm()
    t, adj = round_up(APM_THRESHOLD), round_up(APM_HYSTERESIS)
    for i in range(n):
        if t <= floor[i] or t > ceil[i]:
            continue
        if ol[i] >= t:
            floor[i] = max(floor[i], t - adj)
            ol[i] = max(ol[i], floor[i])
        else:
            ceil[i] = t - STEP
    # cpr3_open_loop_voltage_as_ceiling()
    ceil = list(ol)
    # cpr3_limit_floor_voltages()
    for i in range(n):
        floor[i] = max(round_up(ceil[i] - c["range"][i] * 1000), floor[i])
        ol[i] = max(ol[i], floor[i])
    for i in range(1, n):
        if floor[i] < floor[i - 1]:
            floor[i] = floor[i - 1]
            ol[i] = max(ol[i], floor[i])
            ceil[i] = max(ceil[i], floor[i])
    return {"speed_bin": speed_bin, "raw": raw, "fused": fused,
            "freq": freq, "fuse_corner": fuse_corner, "ol": ol,
            "floor": floor, "ceiling": ceil}


def report(rows):
    print("speed bin %d, CPR fusing revision %d" %
          (param(rows, SPEED_BIN), param(rows, CPR_REV)))
    for name in ("silver", "gold"):
        r = corners(name, rows)
        c = CLUSTERS[name]
        print("\n%s: fused open-loop (before the tree's adjustment)" % name)
        for i, v in enumerate(r["fused"]):
            print("  %-9s raw 0x%02x  %4d mV" % (c["names"][i], r["raw"][i],
                                                 v // 1000))
        print("  corner     MHz  fuse corner   floor  open-loop = ceiling")
        for i in range(len(r["freq"])):
            print("  %2d     %7.1f  %-10s  %4d mV  %4d mV%s" % (
                i + 1, r["freq"][i] / MHZ, c["names"][r["fuse_corner"][i]],
                r["floor"][i] // 1000, r["ol"][i] // 1000,
                "   APM: arrays on APC" if r["ol"][i] >= APM_THRESHOLD
                else ""))


def decode_log(text):
    """The probe's lines: fuse rows, and what they say beside them."""
    rows = {}
    for m in re.finditer(r"fuse row (\d+) \(0x[0-9a-f]+\) = 0x([0-9a-f]+)",
                         text):
        rows[int(m.group(1))] = int(m.group(2), 16)
    regs = {}
    for m in re.finditer(r"cpuprobe: ([a-z-]+)((?: [0-9a-f]{3,5}=[0-9a-f]{8})+)",
                         text):
        for off, v in re.findall(r"([0-9a-f]{3,5})=([0-9a-f]{8})", m.group(2)):
            regs[(m.group(1), int(off, 16))] = int(v, 16)
    return rows, regs


def clocks(regs):
    # HMSS_GPLL0_CLK_SRC, the clusters' "aux": GPLL0 (600 MHz) through
    # an RCG - source 1 GPLL0, half-divider in bits 4:0 (gcc-sdm660.c).
    aux = None
    acfg = regs.get(("gcc", 0x48060))
    if acfg is not None and (acfg >> 8) & 7 == 1:
        d = acfg & 0x1f
        aux = 600.0 / ((d + 1) / 2 if d else 1)
        print("GPLL0 aux (HMSS_GPLL0_CLK_SRC): cfg 0x%08x - %.0f MHz" %
              (acfg, aux))
    for cl in ("silver", "gold"):
        cfg = regs.get((cl + "-glb", 0x54))
        cmd = regs.get((cl + "-glb", 0x50))
        lval = regs.get((cl + "-pll", 0x04))
        mode = regs.get((cl + "-pll", 0x00))
        st = regs.get((cl + "-pll", 0x2c))
        if cfg is None or lval is None:
            continue
        src, div = (cfg >> 8) & 7, cfg & 0x1f
        what = {3: "PLL main", 4: "GPLL0 aux (%s MHz)" % (
                    "%.0f" % aux if aux else "?"), 5: "PLL early",
                0: "XO"}.get(src, "source %d" % src)
        print("%s: RCG cmd 0x%08x cfg 0x%08x - %s, divide %.1f; PLL mode "
              "0x%08x L %d (%.1f MHz), status 0x%08x" % (
                  cl, cmd, cfg, what, (div + 1) / 2, mode, lval & 0xff,
                  (lval & 0xff) * 19.2, st if st is not None else 0))
        sts = regs.get((cl + "-saw", 0xc18))
        vctl = regs.get((cl + "-saw", 0x900))
        if sts is not None:
            print("%s: SAW PMIC_STS 0x%08x (level %d), VCTL 0x%08x" % (
                cl, sts, sts & 0xff, vctl or 0))


def selftest():
    """Zero fuses, worked by hand (hw-cpu.md, section 7)."""
    rows = {r: 0 for r in range(38, 72)}
    s = corners("silver", rows)
    want_ol = [640, 640, 724, 788, 868, 940, 1024, 1060]
    want_floor = [608, 608, 692, 748, 824, 900, 984, 1020]
    got_ol = [v // 1000 for v in s["ol"]]
    got_floor = [v // 1000 for v in s["floor"]]
    ok = got_ol == want_ol and got_floor == want_floor
    print("silver, zero fuses: open-loop %s floor %s: %s" % (
        got_ol, got_floor, "ok" if ok else "WRONG (want %s, %s)" %
        (want_ol, want_floor)))
    # Sign-magnitude: 0x21 is -1 step, 0x01 is +1 step.
    rows[67] = 0x21 << 34          # LowSVS -10 mV
    t = corners("silver", rows)
    ok2 = t["fused"][0] == 634000
    print("silver LowSVS fuse 0x21: %d uV: %s" % (
        t["fused"][0], "ok" if ok2 else "WRONG (want 634000)"))
    rows[67] = 0x01 << 34
    t = corners("silver", rows)
    ok3 = t["fused"][0] == 654000
    print("silver LowSVS fuse 0x01: %d uV: %s" % (
        t["fused"][0], "ok" if ok3 else "WRONG (want 654000)"))
    return 0 if ok and ok2 and ok3 else 1


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        return selftest()
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    rows, regs = decode_log(text)
    need = [38] + list(range(65, 72))
    if all(r in rows for r in need):
        report(rows)
    else:
        print("no fuse rows %s in the log" % [r for r in need if r not in rows])
    print()
    clocks(regs)
    return 0


if __name__ == "__main__":
    sys.exit(main())
