#!/usr/bin/env python3
"""The two system changes under the GPU's buffers, on QEMU: stage 11.6.

1. mmap(2) of a character device that maps nothing (/dev/null) fails with
   ENODEV, and VFS says nothing on the console (it said EINVAL and
   "VFS: mmap regular/blockdev only" before); libc asked the driver
   (MIOCMMAP) on the way.
2. A mapping lent by a driver (fb's buffers, FBIOC_MAP; mali's buffers are
   lent the same way) whose source goes - fb restarted - survives fork(2):
   the page the process had is still there, a page it never had is SIGSEGV.
   Before 11.6 the fork panicked VM.

Builds remapfork.c into DESTDIR/usr/tests/mali, boots the disk root in its
own WORK_DIR (~/obj-evbarm64/work-mali116), starts fb with virtual=1 (QEMU
has no display it knows), and runs both.

    python3 port/test/mali/qemu-remap.py [-k] [-2] [-3] [-s N]

Log: ~/qemu-remap.log.
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
TREE = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.join(HOME, "obj-evbarm64/work-mali116")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/mali"
DOWN = r"panic|assert|MEECHO will now|kernel stack overflow"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    r = subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                         "--sysroot=" + DEST, "-I",
                         os.path.join(TREE, "minix/drivers/video/fb/arch/"
                                      "aarch64"),
                         "-o", os.path.join(out, "remapfork"),
                         os.path.join(HERE, "remapfork.c")])
    if r != 0:
        sys.exit("build failed: remapfork")
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
        self.all = ""

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
            self.all += c
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
    log = open(os.path.join(HOME, "qemu-remap.log"), "w")
    g = Guest(log, a)
    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    def shell(cmd, pattern, timeout=60):
        g.buf = ""
        g.send(cmd + "; echo EN''D=$?")
        out, m = g.expect_re(r"END=(\d+)|" + DOWN, timeout)
        if not m.group(1):
            raise RuntimeError("the system went down: %r" % m.group(0))
        return out

    try:
        g.expect_re(r"login:", 240)
        g.send("root")
        g.expect_re(r"# ", 60)
        g.send("PATH=$PATH:/sbin:/usr/sbin; export PATH")
        g.expect_re(r"# ", 20)
        shell("minix-service up /service/fb -dev /dev/fb0 -args virtual=1",
              r"#")
        time.sleep(3)

        mark = len(g.all)
        out = shell(GUESTDIR + "/remapfork nodev", r"#")
        record("mmap(2) of /dev/null: ENODEV", "REMAPFORK nodev ok" in out,
               out.strip()[-200:])
        record("VFS says nothing about it",
               "mmap regular/blockdev only" not in g.all[mark:],
               "VFS printed its line")

        # The go comes through a FIFO, and only once the new fb has said it
        # is up: a fork before the old one is gone would prove nothing (the
        # source still there), and a timer cannot tell.
        g.buf = ""
        g.send("rm -f /tmp/go; mkfifo /tmp/go; " + GUESTDIR + "/remapfork "
               "fork < /tmp/go > /tmp/rf.out 2>&1 &")
        g.send("exec 3>/tmp/go")
        time.sleep(3)
        out = shell("cat /tmp/rf.out", r"#")
        record("fb's buffers mapped", "READY" in out, out.strip()[-200:])
        g.buf = ""
        g.send("minix-service refresh fb")
        g.expect_re(r"virtual display, \d+x\d+, no controller", 60)
        time.sleep(2)
        shell("echo go >&3; exec 3>&-", r"#")
        time.sleep(4)
        out = shell("cat /tmp/rf.out", r"#")
        record("fork after the source went: no panic, page kept, the "
               "rest SIGSEGV", "REMAPFORK fork ok" in out,
               out.strip()[-300:])
        out = shell("ls / | wc -l", r"#")
        record("the system answers after it", True)

        g.send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError as e:
        record("the run", False, str(e))
    finally:
        g.stop()

    bad = [n for n, ok in results if not ok]
    print("\n%d check(s), %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
