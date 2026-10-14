#!/usr/bin/env python3
"""POSIX threads for Mesa on QEMU: pthtest.c in a booted MEECHO.

Builds pthtest (static, against DESTDIR's libmthread and libgfxcompat) into
DESTDIR/usr/tests/pthread, boots the disk root in its own WORK_DIR
(~/obj-evbarm64/work-pthread), runs each part on its own - so that one that
hangs or crashes does not hide the others - and then libmthread's old test,
test59 (it must be in DESTDIR already, linked against the same library and
built as the tree builds it: with -fno-builtin, or -O2 drops the memset
of a thread's stack array that its subtest 11 looks for).

    python3 port/test/pthread/qemu-pthread.py [-k] [-2] [-3] [-s N]

Log: ~/qemu-pthread.log.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-pthread")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/pthread"
PARTS = ("mutex", "timed", "cond", "barrier", "cpu", "c11")
DOWN = r"panic|assert|MEECHO will now|kernel stack overflow"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    r = subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                         "--sysroot=" + DEST,
                         "-I" + os.path.join(DEST, "usr/include/gfxcompat"),
                         "-o", os.path.join(out, "pthtest"),
                         os.path.join(HERE, "pthtest.c"),
                         "-lgfxcompat", "-lmthread"])
    if r != 0:
        sys.exit("build failed: pthtest")
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
    log = open(os.path.join(HOME, "qemu-pthread.log"), "w")
    g = Guest(log, a)
    total = bad = 0

    def shell(cmd, timeout=180):
        g.buf = ""
        g.send(cmd + "; echo EN''D=$?")
        out, m = g.expect_re(r"END=(\d+)|" + DOWN, timeout)
        if not m.group(1):
            raise RuntimeError("the system went down: %r" % m.group(0))
        return out, int(m.group(1))

    try:
        g.expect_re(r"login:", 240)
        g.send("root")
        g.expect_re(r"# ", 60)
        for part in PARTS:
            out, r = shell("%s/pthtest %s 2>&1 | tail -40" % (GUESTDIR, part))
            print("--- %s (exit %d)" % (part, r))
            for line in out.splitlines():
                if line.startswith(("ok  ", "FAIL", "PTHTEST")) or \
                        "signal" in line.lower() or "fault" in line.lower():
                    print(line)
            m = re.search(r"PTHTEST (\d+) checks, (\d+) failed", out)
            if m:
                total += int(m.group(1))
                bad += int(m.group(2))
            else:
                print("FAIL %s: no summary - the program died" % part)
                bad += 1
        out, r = shell("cd /usr/tests/minix-posix; ./test59 > /tmp/t59 2>&1;"
                       " s=$?; cd /; tail -5 /tmp/t59; test $s = 0", 600)
        print("--- test59 (exit %d)" % r)
        print("\n".join(l for l in out.splitlines()[1:] if l.strip()))
        total += 1
        if r != 0 or "ok" not in out:
            print("FAIL test59")
            bad += 1
        out, r = shell("ls / | wc -l")
        print("the system answers after it: %s" % ("yes" if r == 0 else "no"))
        g.send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError as e:
        print("FAIL the run: %s" % e)
        bad += 1
    finally:
        g.stop()
    print("PTHREAD: %d check(s) in all parts, %d failure(s)" % (total, bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
