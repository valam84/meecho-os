#!/usr/bin/env python3
"""The Wayland stack on QEMU (stage 11.4): a compositor and a client, with
no screen.

Runs what the tree installs into DESTDIR (port/build-dirs.sh over the
stack's directories, port/gfx/README): boots the disk root in its own
WORK_DIR (~/obj-evbarm64/work-gfx) and, with no frame buffer driver
started, runs

    meechowm -s 'wlhello ...'

- meechowm finds no screen and draws into a headless output in memory,
with wlroots' pixman renderer; wlhello connects over the Wayland socket,
passes a memfd buffer, and waits until the compositor has drawn its frame.
The check is wlhello's own line, "WLHELLO drawn ...", its exit status, and
meechowm's log around it (~/qemu-gfx.log).  The quick half of
qemu-meechowm.py: no fb, no input, no fonts.

    python3 port/gfx/test/qemu-gfx.py [-k] [-s cores] [-t seconds]
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
WORK = os.path.join(HOME, "obj-evbarm64/work-gfx")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
INSTALLED = ["usr/bin/meechowm", "usr/tests/gfx/wlhello",
             "usr/share/X11/xkb/rules/evdev"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-t", type=int, default=90, help="seconds for the run")
    a = ap.parse_args()

    for f in INSTALLED:
        if not os.path.exists(os.path.join(DEST, f)):
            sys.exit("not in DESTDIR: /%s (port/build-dirs.sh first)" % f)
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="1500",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-gfx.log"), "w")
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

    rc = 1
    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        # Short lines: tty's canonical buffer cuts at 255 bytes.
        send("mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg")
        # By pid, not %1: ash's kill %N signals the job's process group,
        # and PM has no groups apart from sessions.
        send("meechowm -s \"/usr/tests/gfx/wlhello -t 3; "
             "echo HELLO-RC=\\$?\" > /tmp/mwm.log 2>&1 & MWM=$!")
        send("sleep %d; tail -40 /tmp/mwm.log; echo LOG''END" % a.t)
        out = expect(r"LOGEND", a.t + 300)
        drawn = re.search(r"WLHELLO drawn .*", out)
        hello_rc = re.search(r"HELLO-RC=(\d+)", out)
        print("meechowm: %s" % ("headless" if "no screen: headless" in out
                                else "screen?"))
        print("wlhello: %s" % (drawn.group(0) if drawn else "no WLHELLO line"))
        print("wlhello exit: %s" % (hello_rc.group(1) if hello_rc else "?"))
        rc = 0 if drawn and hello_rc and hello_rc.group(1) == "0" else 1
        send("kill $MWM; sleep 2; /sbin/poweroff")
        time.sleep(5)
    except RuntimeError as e:
        print("FAIL: %s" % e)
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    print("ok" if rc == 0 else "FAIL")
    sys.exit(rc)


if __name__ == "__main__":
    main()
