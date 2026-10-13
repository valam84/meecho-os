#!/usr/bin/env python3
"""The compositor's path to the screen on QEMU (stage 11.4): fbflip.

Builds fbflip.c into DESTDIR/usr/tests/fb, boots the disk root in its own
WORK_DIR (~/obj-evbarm64/work-fb), starts the frame buffer driver with
virtual=1 - no display controller, the scanout buffers only - and runs
fbflip: FBIOC_MAP (the driver's contiguous buffers, through VM's shared
memory, into fbflip), drawing, FBIOC_FLIP, and the screen read back with
pread(2).  Twice, so that the second FBIOC_MAP of a driver that already
made its buffers is covered too.  Log: ~/qemu-fbflip.log.

    python3 port/test/fb/qemu-fbflip.py [-k] [-s cores]
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
WORK = os.path.join(HOME, "obj-evbarm64/work-fb")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/fb"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    if subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                        "--sysroot=" + DEST, "-o",
                        os.path.join(out, "fbflip"),
                        os.path.join(HERE, "fbflip.c")]) != 0:
        sys.exit("build failed: fbflip")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    a = ap.parse_args()

    if not a.k:
        build()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="900",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-fbflip.log"), "w")
    p = subprocess.Popen(["bash", RAMIMAGE, "-d"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         bufsize=0, env=env, start_new_session=True)
    buf = ""

    def expect(pat, timeout):
        nonlocal buf
        deadline = time.time() + timeout
        while True:
            m = re.search(pat, buf)
            if m:
                out = buf[:m.end()]
                buf = buf[m.end():]
                return out
            if time.time() > deadline or p.poll() is not None:
                raise RuntimeError("no %r" % pat)
            if select.select([p.stdout], [], [], 0.2)[0]:
                c = p.stdout.read(1).decode(errors="replace")
                buf += c
                log.write(c)
                log.flush()

    def send(line):
        p.stdin.write((line + "\n").encode())
        p.stdin.flush()
        time.sleep(0.4)

    results = []
    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("minix-service up /service/fb -dev /dev/fb0 -args virtual=1; "
             "echo FB''UP=$?")
        out = expect(r"FBUP=\d+", 60)
        results.append(("fb starts with virtual=1", "FBUP=0" in out))
        for n in (1, 2):
            send("%s/fbflip; echo EN''D=$?" % GUESTDIR)
            out = expect(r"END=\d+|panic", 300)
            for line in out.splitlines():
                if line.startswith(("ok", "FAIL", "FBFLIP", "      ")):
                    print(line)
            results.append(("fbflip, run %d" % n, "END=0" in out))
        send("/sbin/poweroff")
        time.sleep(4)
    except RuntimeError as e:
        results.append(("the run", False))
        print("FAIL: %s" % e)
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    for name, ok in results:
        print("%s  %s" % ("ok  " if ok else "FAIL", name))
    bad = [n for n, ok in results if not ok]
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
