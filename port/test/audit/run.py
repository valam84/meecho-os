#!/usr/bin/env python3
"""Server-audit probes on QEMU: does an ordinary user bring the system down?

Each probe in this directory (*.c) is built with the cross compiler into
DESTDIR/usr/tests/audit, the disk image is remade so that it carries them,
and each is run in turn from a root shell - the probe itself gives up root
first thing (audit.h). After each one the runner asks the shell whether it
is still there. Outcomes:

  SURVIVED  the shell answered; the probe's own exit status is printed next
            to it (0 - it did what it tried and nothing broke).
  KILLED    no shell: a server panicked or asserted and the machine went
            down. The console lines around "panic" are kept in the log and
            the guest is logged into again after it reboots.
  TIMEOUT   the probe did not finish in time but the system answers ^C.

Usage (in WSL):
    python3 port/test/audit/run.py [-t probe,probe] [-T seconds] [-n]
      -n  do not rebuild the disk image (probes already on it)

Log: ~/audit-qemu.log; report on stdout.
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
GUESTDIR = "/usr/tests/audit"

spec = importlib.util.spec_from_file_location(
    "qemu_tests", os.path.join(HERE, "..", "suite", "qemu-tests.py"))
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
qt.TESTDIR = GUESTDIR


def build(names):
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    for n in names:
        src = os.path.join(HERE, n + ".c")
        r = subprocess.call([XCC, "-O", "-static", "-Wall", "-Wno-unused",
                             "--sysroot=" + DEST, "-I" + HERE,
                             "-o", os.path.join(out, n), src])
        if r != 0:
            sys.exit("build failed: " + n)


def run_one(g, name, timeout):
    g.buf = ""
    g.send("./%s; echo EN''D-%s=$?" % (name, name))
    try:
        out = g.expect("END-%s=" % name, timeout)
        tail = g.expect("\n", 10)
        status = re.match(r"(\d+)", tail)
        status = status.group(1) if status else "?"
    except RuntimeError:
        out = g.buf
        status = None
    died = re.search(r"panic|assert|MEECHO will now|kernel stack overflow",
                     out)
    if status is not None and not died and g.alive():
        return "SURVIVED", status, out
    if status is None and not died:
        g.buf = ""
        g.interrupt()
        if g.alive():
            return "TIMEOUT", "-", out
    # Down: keep what the console said, wait for the reboot, log in again.
    try:
        more = g.expect("login:", 240)
        out += more
        g.buf = "login:"
        g.login_at_prompt()
    except RuntimeError:
        g.kill_group()
        raise
    return "KILLED", "-", out


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

    log = open(os.path.join(HOME, "audit-qemu.log"), "w")
    g = qt.Guest(log, timeout=7200)
    os.environ.pop("DISK_FRESH", None)
    g.login()
    results = []
    for n in names:
        verdict, status, out = run_one(g, n, a.T)
        results.append((n, verdict, status))
        print("%-24s %-9s %s" % (n, verdict, status), flush=True)
        if verdict == "KILLED":
            for line in out.splitlines():
                if re.search(r"panic|assert|stack|pc 0x|failed", line):
                    print("    " + line.strip())
    g.close()
    killed = sum(1 for r in results if r[1] == "KILLED")
    print("\n%d probes, %d brought the system down" % (len(results), killed))


if __name__ == "__main__":
    main()
