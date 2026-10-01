#!/usr/bin/env python3
"""Journal probes on QEMU, root on the MFS V4 disk.

Each *.c here is built into DESTDIR/usr/tests/journal, the disk image is
remade so that it carries them, and each probe runs as root in /root (on the
journaled root volume). A probe that does not answer in time is reported as
HANG, together with the console lines that name a dead process - the root
file system dying takes the whole machine with it, quietly.

    python3 port/test/journal/run.py [-t probe,probe] [-T seconds] [-n]
      -n  do not rebuild the disk image (probes already on it)

Log: ~/journal-qemu.log; report on stdout.
"""

import argparse
import glob
import importlib.util
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/journal"

spec = importlib.util.spec_from_file_location(
    "qemu_tests", os.path.join(HERE, "..", "suite", "qemu-tests.py"))
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)


def build(names):
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    for n in names:
        r = subprocess.call([XCC, "-O", "-static", "-Wall",
                             "--sysroot=" + DEST,
                             "-o", os.path.join(out, n),
                             os.path.join(HERE, n + ".c")])
        if r != 0:
            sys.exit("build failed: " + n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-t", help="comma-separated probes (default: all)")
    ap.add_argument("-T", type=int, default=120, help="seconds per probe")
    ap.add_argument("-n", action="store_true", help="keep the disk image")
    a = ap.parse_args()

    names = sorted(os.path.basename(p)[:-2]
                   for p in glob.glob(os.path.join(HERE, "*.c")))
    if a.t:
        names = [n for n in a.t.split(",") if n]
    build(names)
    if not a.n:
        os.environ["DISK_FRESH"] = "1"

    log = open(os.path.join(HOME, "journal-qemu.log"), "w")
    g = qt.Guest(log, timeout=3600)
    os.environ.pop("DISK_FRESH", None)
    g.login()
    g.send("cd /root")
    g.expect("# ", 30)
    bad = 0
    for n in names:
        g.buf = ""
        g.send("%s/%s; echo EN''D-%s=$?" % (GUESTDIR, n, n))
        try:
            out = g.expect("END-%s=" % n, a.T)
            out += g.expect("\n", 10)
            m = re.search(r"END-%s=(\d+)" % re.escape(n), out)
            verdict = "EXIT %s" % (m.group(1) if m else "?")
            if not m or m.group(1) != "0":
                bad += 1
        except RuntimeError:
            out = g.buf
            verdict = "HANG"
            bad += 1
        print("%-16s %s" % (n, verdict), flush=True)
        for line in out.splitlines():
            if re.search(r"lost|flushcache:|SIGSEGV|panic|pc 0x|journal:",
                         line):
                print("    " + line.strip(), flush=True)
        if verdict == "HANG":
            break
    if bad and verdict == "HANG":
        g.kill_group()
    else:
        g.close()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
