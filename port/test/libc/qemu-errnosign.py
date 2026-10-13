#!/usr/bin/env python3
"""The sign of errno after a libc call fails (errnosign.c) on QEMU.

A libc file built with _SYSTEM sees <errno.h>'s codes as the servers'
negative ones, while errno in a process is positive; "errno = ENOSYS" in
such a file stores -78 (the SysV IPC calls did).  errnosign.c is built
WITHOUT _SYSTEM and asks, after each failing call, what errno holds.

Without arguments it builds errnosign.c against DESTDIR's libc (so: after
libc was rebuilt and installed) and runs it.  Arguments NAME=PATH name
ready-made binaries instead ("NAME=" alone: build errnosign.c under that
name; for the installed ipcrm: ipcrm=$DESTDIR/usr/bin/ipcrm) (a NAME with "ipcrm" in it is run as
"ipcrm -m 1" and must say "Function not implemented"); one whose NAME
starts with "before" is
expected to FAIL - it is the defect, reproduced - and counts as ok only if
it does.  That is how the fix is shown from a tree whose DESTDIR libc.a is
not to be touched: link one binary against the old libc.a and one with the
patched objects in front of it.

The guest is the disk root, made in its own WORK_DIR from a private sparse
DESTDIR (~/dest-errnosign: METALOG and /usr/tests only), so nothing shared
is written and a QEMU of another session is not in the way.

    python3 port/test/libc/qemu-errnosign.py [-2] [-3] [-s N] [NAME=PATH ...]

Log: ~/qemu-errnosign.log.
"""

import argparse
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.join(HOME, "obj-evbarm64/work-errnosign")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
PRIV = os.path.join(HOME, "dest-errnosign")
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/errnosign"
DOWN = r"panic|assert|MEECHO will now|kernel stack overflow"


def stage(binaries):
    """Make the private DESTDIR with the binaries in /usr/tests/errnosign."""
    if os.path.isdir(PRIV):
        shutil.rmtree(PRIV)
    out = os.path.join(PRIV, GUESTDIR.lstrip("/"))
    os.makedirs(out)
    shutil.copy(os.path.join(DEST, "METALOG"), os.path.join(PRIV, "METALOG"))
    for name, path in binaries:
        if path is None:
            r = subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                                 "--sysroot=" + DEST,
                                 "-o", os.path.join(out, name),
                                 os.path.join(HERE, "errnosign.c")])
            if r != 0:
                sys.exit("build failed")
        else:
            shutil.copy(path, os.path.join(out, name))
        os.chmod(os.path.join(out, name), 0o755)


class Guest(object):
    def __init__(self, log, args):
        env = dict(os.environ)
        env["WORK_DIR"] = WORK
        env["DESTDIR"] = PRIV
        env["QEMU_TIMEOUT"] = "600"
        env["QEMU_SMP"] = str(args.s)
        env["DISK_FRESH"] = "1"
        env["DISK_MB"] = "64"
        cmd = ["bash", RAMIMAGE, "-d"]
        if args.el2:
            cmd.append("-2")
        if args.gic3:
            cmd.append("-3")
        self.log = log
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, bufsize=0,
                                  env=env, start_new_session=True)
        self.buf = ""

    def expect_re(self, pattern, timeout):
        deadline = time.time() + timeout
        while True:
            m = re.search(pattern, self.buf)
            if m:
                out = self.buf[:m.end()]
                self.buf = self.buf[m.end():]
                return out, m
            if time.time() >= deadline:
                raise RuntimeError("no %r in %.0f s" % (pattern, timeout))
            if self.p.poll() is not None:
                raise RuntimeError("QEMU exited waiting for %r" % pattern)
            if not select.select([self.p.stdout], [], [], 0.2)[0]:
                continue
            ch = self.p.stdout.read(1)
            if not ch:
                continue
            c = ch.decode(errors="replace")
            self.buf += c
            self.log.write(c)
            self.log.flush()

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()
        time.sleep(0.35)

    def stop(self):
        try:
            os.killpg(os.getpgid(self.p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-2", dest="el2", action="store_true", help="EL2 entry")
    ap.add_argument("-3", dest="gic3", action="store_true", help="GICv3")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("bins", nargs="*", metavar="NAME=PATH")
    a = ap.parse_args()

    binaries = []
    for b in a.bins:
        name, eq, path = b.partition("=")
        if not eq:
            sys.exit("not NAME=PATH: " + b)
        binaries.append((name, path or None))	# NAME= : build errnosign.c
    if not binaries:
        binaries = [("errnosign", None)]
    stage(binaries)
    os.makedirs(WORK, exist_ok=True)
    log = open(os.path.join(HOME, "qemu-errnosign.log"), "w")
    g = Guest(log, a)
    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    def run(name, expect_fail):
        # "ipcrm*": the real program, what a person sees (its warn() prints
        # strerror(errno)); anything else is errnosign itself.
        ipcrm = "ipcrm" in name
        g.buf = ""
        g.send(GUESTDIR + "/" + name + (" -m 1 2>&1" if ipcrm else "") +
               "; echo EN''D=$?")
        try:
            out, m = g.expect_re(r"END=(\d+)|" + DOWN, 120)
        except RuntimeError as e:
            record(name, False, str(e))
            return
        if not m.group(1):
            record(name, False, "the system went down: %r" % m.group(0))
            raise RuntimeError("down")
        for line in out.splitlines():
            if re.match(r"(ok  |FAIL|ENOSYS|ipc|ERRNOSIGN)", line) or \
                    (ipcrm and "shmid" in line):
                print("      " + line.rstrip())
        if ipcrm:
            failed = 0 if "Function not implemented" in out else 1
            what = "ipcrm -m 1 did not say 'Function not implemented'"
        else:
            s = re.search(r"ERRNOSIGN checks (\d+) failed (\d+)", out)
            if not s:
                record(name, False, "no summary; exit %s" % m.group(1))
                return
            failed = int(s.group(2))
            what = "%d of %s checks failed" % (failed, s.group(1))
        if expect_fail:
            record(name + " (defect expected)", failed > 0,
                   "nothing failed - the defect is not reproduced")
        else:
            record(name, failed == 0, what)

    try:
        g.expect_re(r"login:", 240)
        g.send("root")
        g.expect_re(r"# ", 60)
        g.send("PATH=$PATH:/sbin:/usr/sbin; export PATH")
        g.expect_re(r"# ", 20)

        for name, _ in binaries:
            run(name, name.startswith("before"))

        g.send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError as e:
        record("the guest ran the checks", False, str(e))
    finally:
        g.stop()

    if not results:
        record("the guest ran the checks", False, "nothing ran")
    bad = [n for n, ok in results if not ok]
    print("\n%d check(s), %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
