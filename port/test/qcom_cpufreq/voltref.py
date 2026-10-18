#!/usr/bin/env python3
"""Reference corners for the qcom_cpufreq stand, by port/clover/cpu-volt.py
- the arithmetic's reference - and not by the service's own code.

    voltref.py PROBE_LOG > volt-ref.txt

One block per fuse set: its rows, then each cluster's corners as cpu-volt.py
computes them (kHz, open loop uV, floor uV), or "none" for a speed bin the
vendor tree has no corners for.  The sets:

    tablet     the Mi Pad 4's rows, from the probe's log (hw-cpu.md, s. 7)
    zero       all zero (cpu-volt.py --selftest)
    bin2       a speed bin with no table
    apm872     a chip with a point a PLL makes at exactly 872 mV - the APM
               threshold, which path A must refuse
    edge868    one with a point at exactly 868 mV - which a rail at 868
               holds
    randomN    random rows in each of the bins the tree has (0, 1, 3)
"""
import importlib.util
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CPU_VOLT = os.path.join(HERE, "..", "..", "clover", "cpu-volt.py")
ROWS = [38] + list(range(65, 72))
XO_KHZ = 19200
NRANDOM = 300


def load():
    spec = importlib.util.spec_from_file_location("cpu_volt", CPU_VOLT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def pll_point(khz):
    """A point a cluster's PLL makes: L >= 43 (PLL_MIN_LVAL)."""
    return khz % XO_KHZ == 0 and khz // XO_KHZ >= 43


def emit(cv, name, rows):
    print("set %s" % name)
    for r in ROWS:
        print("row %d 0x%016x" % (r, rows.get(r, 0)))
    for cl, cname in ((0, "silver"), (1, "gold")):
        try:
            c = cv.corners(cname, rows)
        except ValueError:
            print("cluster %d none" % cl)
            continue
        print("cluster %d bin %d n %d" % (cl, c["speed_bin"], len(c["freq"])))
        for f, ol, fl in zip(c["freq"], c["ol"], c["floor"]):
            print("corner %d %d %d" % (f // 1000, ol, fl))
    print("end")


def has_point_at(cv, rows, uv):
    for cname in ("silver", "gold"):
        try:
            c = cv.corners(cname, rows)
        except ValueError:
            return False
        for f, ol in zip(c["freq"], c["ol"]):
            if ol == uv and pll_point(f // 1000):
                return True
    return False


def random_rows(rng, speed_bin):
    rows = {r: rng.getrandbits(64) for r in ROWS}
    rows[38] = (rows[38] & ~(7 << 29)) | (speed_bin << 29)
    return rows


def search(cv, rng, uv):
    for _ in range(200000):
        rows = random_rows(rng, rng.choice((0, 1, 3)))
        if has_point_at(cv, rows, uv):
            return rows
    raise SystemExit("voltref: no fuse set with a point at %d uV" % uv)


def main():
    if len(sys.argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    cv = load()
    text = open(sys.argv[1], encoding="utf-8", errors="replace").read()
    tablet, _ = cv.decode_log(text)
    if not all(r in tablet for r in ROWS):
        raise SystemExit("voltref: the log has no fuse rows %s" %
                         [r for r in ROWS if r not in tablet])
    emit(cv, "tablet", tablet)
    emit(cv, "zero", {r: 0 for r in ROWS})
    emit(cv, "bin2", {38: 2 << 29})
    rng = random.Random(20261018)
    emit(cv, "apm872", search(cv, rng, 872000))
    emit(cv, "edge868", search(cv, rng, 868000))
    for i in range(NRANDOM):
        emit(cv, "random%d" % i, random_rows(rng, (0, 1, 3)[i % 3]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
