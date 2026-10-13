#!/usr/bin/env python3
"""libepoll-shim on QEMU (milestone 11.3): shimtest, the real thing.

Builds shimtest.c against the installed library (-lepoll-shim, headers
from /usr/include/libepoll-shim) into DESTDIR/usr/tests/evloop, boots
the disk root in its own WORK_DIR (~/obj-evbarm64/work-evloop) and runs
it.  The same test runs on the host against the library's sources
(run.sh); here the poll(2), pipes, signals and clocks underneath are
MEECHO's.

    python3 port/test/evloop/qemu-shim.py [-k] [-s cores]

Log: ~/qemu-shim.log.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-evloop")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/evloop"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    if subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                        "--sysroot=" + DEST,
                        "-I" + os.path.join(DEST, "usr/include/libepoll-shim"),
                        "-o", os.path.join(out, "shimtest"),
                        os.path.join(HERE, "shimtest.c"),
                        "-lepoll-shim"]) != 0:
        sys.exit("build failed: shimtest")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    a = ap.parse_args()

    if not a.k:
        build()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="1200",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-shim.log"), "w")
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
        time.sleep(0.35)

    rc = 1
    try:
        expect(r"login:", 240)
        send("root")
        expect(r"# ", 60)
        send("%s/shimtest; echo EN''D=$?" % GUESTDIR)
        out = expect(r"END=\d+", 600)
        for line in out.splitlines():
            if line.startswith(("ok", "FAIL", "SHIM")):
                print(line)
        rc = 0 if "END=0" in out else 1
        send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError as e:
        print("FAIL: %s" % e)
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    sys.exit(rc)


if __name__ == "__main__":
    main()
