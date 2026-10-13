#!/usr/bin/env python3
"""cdevfd(2) on QEMU: the VFS back-call under PRIME and sync files.

A driver gives a process waiting on it a descriptor of one of its own
minors, or asks which of its minors a descriptor of that process is - what
the mali service does for DRM_IOCTL_PRIME_* and DRM_IOCTL_SYNCOBJ_*_FD.
Without a GPU: cdevfdtest (a driver that does nothing else) and cdevfdt (its
client) - dup, fork, exec, close-on-exec, a pass over a socket, lseek to
the end, fstat, the refusals, and the driver's count of opens after each.

Builds both into DESTDIR/usr/tests/cdevfd (the driver with nbmake, against
DESTDIR's libsys, which must have cdevfd.o), boots the disk root in its own
WORK_DIR (~/obj-evbarm64/work-cdevfd), and runs the client.  VFS comes from
the build's obj, as the boot image always does.

    python3 port/test/cdevfd/qemu-cdevfd.py [-k] [-2] [-3] [-s N]

Log: ~/qemu-cdevfd.log.
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
TREE = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.join(HOME, "obj-evbarm64/work-cdevfd")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
NBMAKE = os.environ.get("NBMAKE", os.path.join(
    HOME, "tools-evbarm64/bin/nbmake-evbarm64-el"))
GUESTDIR = "/usr/tests/cdevfd"
DOWN = r"panic|assert|MEECHO will now|kernel stack overflow"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    for step in (["obj"], ["dependall"]):
        if subprocess.call([NBMAKE, "-C", HERE] + step) != 0:
            sys.exit("build failed: cdevfdtest (%s)" % step[0])
    obj = subprocess.check_output([NBMAKE, "-C", HERE, "-V", ".OBJDIR"],
                                  universal_newlines=True).strip()
    shutil.copy(os.path.join(obj, "cdevfdtest"), out)
    shutil.copy(os.path.join(HERE, "cdevfdtest.conf"), out)
    r = subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                         "--sysroot=" + DEST, "-o",
                         os.path.join(out, "cdevfdt"),
                         os.path.join(HERE, "cdevfdt.c")])
    if r != 0:
        sys.exit("build failed: cdevfdt")
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
    log = open(os.path.join(HOME, "qemu-cdevfd.log"), "w")
    g = Guest(log, a)
    ok = False

    def shell(cmd, timeout=120):
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
        g.send("PATH=$PATH:/sbin:/usr/sbin; export PATH")
        g.expect_re(r"# ", 20)
        shell("mknod /dev/cdevfdtest c 17 0")
        out, r = shell("minix-service up %s/cdevfdtest -dev /dev/cdevfdtest "
                       "-config %s/cdevfdtest.conf" % (GUESTDIR, GUESTDIR))
        if r != 0:
            raise RuntimeError("the driver did not start: " + out[-300:])
        out, r = shell(GUESTDIR + "/cdevfdt")
        for line in out.splitlines():
            if line.startswith(("ok  ", "FAIL", "CDEVFDT")):
                print(line)
        m = re.search(r"CDEVFDT (\d+) checks, (\d+) failed", out)
        ok = m is not None and m.group(2) == "0" and r == 0
        out, r = shell("ls / | wc -l")
        print("the system answers after it: %s" % ("yes" if r == 0 else
                                                    "no"))
        g.send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError as e:
        print("FAIL the run: %s" % e)
        ok = False
    finally:
        g.stop()
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
