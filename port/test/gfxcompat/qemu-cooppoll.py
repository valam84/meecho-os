#!/usr/bin/env python3
"""libgfxcompat's shared poll(2) on QEMU: cooppoll, twice.

Builds port/test/gfxcompat/cooppoll.c against the installed libraries
(libgfxcompat's <poll.h> redirects poll() to gfx_poll(); libepoll-shim
waits in it when the program has threads) into DESTDIR/usr/tests/gfxcompat,
and a second time with -DGFXCOMPAT_NO_POLL, where poll() is the plain call.
Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-cooppoll) and
runs both: the first must pass every check, the second must stop at "ring"
- the main thread's poll(2) blocks the process and the ring's threads never
run - which shows the test sees what it tests.

    python3 port/test/gfxcompat/qemu-cooppoll.py [-k] [-s cores]

Log: ~/qemu-cooppoll.log.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-cooppoll")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/gfxcompat"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    for name, extra in (("cooppoll", []),
                        ("cooppoll-plain", ["-DGFXCOMPAT_NO_POLL"])):
        if subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                            "--sysroot=" + DEST] + extra +
                           ["-I" + os.path.join(DEST, "usr/include/gfxcompat"),
                            "-I" + os.path.join(DEST,
                                                "usr/include/libepoll-shim"),
                            "-o", os.path.join(out, name),
                            os.path.join(HERE, "cooppoll.c"),
                            "-lepoll-shim", "-lgfxcompat", "-lmthread"]) != 0:
            sys.exit("build failed: " + name)


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
    log = open(os.path.join(HOME, "qemu-cooppoll.log"), "w")
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

    def run(name):
        send("%s/%s; echo EN''D=$?" % (GUESTDIR, name))
        out = expect(r"END=\d+", 300)
        lines = [l for l in out.splitlines()
                 if l.startswith(("ok", "FAIL", "COOPPOLL"))]
        for l in lines:
            print("      " + l)
        return out, lines

    results = []
    try:
        expect(r"login:", 240)
        send("root")
        expect(r"# ", 60)
        out, lines = run("cooppoll")
        results.append(("gfx_poll: every check", "END=0" in out and
                        any(l.startswith("COOPPOLL") and " 0 failed" in l
                            for l in lines)))
        out, lines = run("cooppoll-plain")
        results.append(("plain poll(2): stops at the ring, as it must",
                        "END=1" in out and any("stopped at the ring" in l
                                               for l in lines)))
        send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError as e:
        results.append(("the run: %s" % e, False))
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    for name, ok in results:
        print("%s  %s" % ("ok  " if ok else "FAIL", name))
    bad = [n for n, ok in results if not ok]
    print("\n%d check(s), %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
