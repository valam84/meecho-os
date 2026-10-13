#!/usr/bin/env python3
"""Memory objects (memfd_create(2)) on QEMU: milestone 11.2.

Builds memfdtest.c into DESTDIR/usr/tests/memfd, so that the disk image
has it, boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-memfd;
it can run beside another session's QEMU), and runs it twice: as root, and
as an ordinary user (uid 9999, memfdtest -u), because a memory object is a
thing every client makes and nothing about it may need privilege.  After
each run the system is asked a question through VFS, to show it is still
there.

    python3 port/test/memfd/qemu-memfd.py [-k] [-2] [-3] [-s N]

-k keeps the disk image (and does not rebuild memfdtest).  Log:
~/qemu-memfd.log.
"""

import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.join(HOME, "obj-evbarm64/work-memfd")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/memfd"
DOWN = r"panic|assert|MEECHO will now|kernel stack overflow"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    for prog in ("memfdtest",):
        r = subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                             "--sysroot=" + DEST,
                             "-o", os.path.join(out, prog),
                             os.path.join(HERE, prog + ".c")])
        if r != 0:
            sys.exit("build failed: " + prog)
    os.chmod(out, 0o755)


class Guest(object):
    def __init__(self, log, args):
        env = dict(os.environ)
        env["WORK_DIR"] = WORK
        env["QEMU_TIMEOUT"] = "900"
        env["QEMU_SMP"] = str(args.s)
        if not args.k:
            env["DISK_FRESH"] = "1"
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
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-2", dest="el2", action="store_true", help="EL2 entry")
    ap.add_argument("-3", dest="gic3", action="store_true", help="GICv3")
    ap.add_argument("-s", type=int, default=1, help="cores")
    a = ap.parse_args()

    if not a.k:
        build()
    os.makedirs(WORK, exist_ok=True)
    log = open(os.path.join(HOME, "qemu-memfd.log"), "w")
    g = Guest(log, a)
    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    def run(name, cmd, timeout=600):
        g.buf = ""
        g.send(cmd + "; echo EN''D=$?")
        try:
            out, m = g.expect_re(r"END=(\d+)|" + DOWN, timeout)
        except RuntimeError as e:
            record(name, False, str(e))
            return ""
        if not m.group(1):
            record(name, False, "the system went down: %r" % m.group(0))
            raise RuntimeError("down")
        s = re.search(r"MEMFD checks (\d+) failed (\d+)", out)
        fails = [l for l in out.splitlines() if l.startswith("FAIL")]
        if not s:
            record(name, False, "no summary; exit %s" % m.group(1))
        elif s.group(2) != "0":
            record(name, False, "%s of %s checks failed: %s" %
                   (s.group(2), s.group(1), "; ".join(fails)))
        else:
            record(name, True)
            print("      (%s checks)" % s.group(1))
        return out

    def alive(name):
        g.buf = ""
        g.send("ls / | wc -l; echo AL''IVE")
        try:
            g.expect_re(r"ALIVE", 30)
            g.expect_re(r"# ", 20)
            record(name, True)
        except RuntimeError as e:
            record(name, False, str(e))

    try:
        g.expect_re(r"login:", 240)
        g.send("root")
        g.expect_re(r"# ", 60)
        g.send("PATH=$PATH:/sbin:/usr/sbin; export PATH")
        g.expect_re(r"# ", 20)

        run("memfdtest as root", GUESTDIR + "/memfdtest")
        alive("VFS answers after it")
        run("memfdtest as uid 9999", GUESTDIR + "/memfdtest -u")
        alive("and again")

        g.send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError:
        pass
    finally:
        g.stop()

    bad = [n for n, ok in results if not ok]
    print("\n%d check(s), %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
