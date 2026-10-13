#!/usr/bin/env python3
"""pollbench on QEMU: what waiting on N descriptors costs (milestone 11.3).

Builds pollbench.c into DESTDIR/usr/tests/evloop (so the disk image has
it), boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-evloop)
and runs "pollbench N..." - by default 1 4 16 64 - printing its POLL
lines.  Under TCG the absolute numbers are inflated; what is read from
them is how the cost grows with N, against the board's numbers
(port/board-cc.sh -r port/test/evloop/pollbench.c ...).

    python3 port/test/evloop/qemu-pollbench.py [-k] [-s cores] [-r rounds]
                                               [N ...]

Log: ~/qemu-pollbench.log.
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
                        "--sysroot=" + DEST, "-o",
                        os.path.join(out, "pollbench"),
                        os.path.join(HERE, "pollbench.c")]) != 0:
        sys.exit("build failed: pollbench")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-r", type=int, default=2000, help="rounds")
    ap.add_argument("n", nargs="*", default=["1", "4", "16", "64"])
    a = ap.parse_args()

    if not a.k:
        build()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="1200",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-pollbench.log"), "w")
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
        send("%s/pollbench -r %d %s; echo EN''D=$?" %
             (GUESTDIR, a.r, " ".join(a.n)))
        out = expect(r"END=\d+", 1000)
        for line in out.splitlines():
            if line.startswith("POLL"):
                print("cores %d  %s" % (a.s, line))
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
