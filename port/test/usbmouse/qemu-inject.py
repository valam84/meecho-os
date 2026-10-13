#!/usr/bin/env python3
"""A mouse on QEMU: the half of the USB mouse that has nothing to do with
USB.

QEMU's virt machine has no xHCI this port drives, so the mouse itself is
the board's business.  Everything above it is not: usb_mouse's reports
(report.c), libinputdriver, the input server and its mouse devices - the
other side of which is what the compositor will read.  usb_mouse's
inject=REPORTS puts boot reports through exactly that path, and mouseev
(built here into DESTDIR/usr/tests/usbmouse, so it is on the disk) reads
/dev/mousemux and sums up what arrived.

  moves and clicks    -> x 8 y -2 wheel 3, b1 and b3 pressed and released
  wheel=0             -> the fourth byte is not a wheel: nothing
  a button held when  -> it comes up when the driver goes (SIGTERM)
  the driver stops
  /dev/mouse0         -> the device of its own, not only the mux
  the keyboard        -> its key events do not come out of the mouse

Runs in its own WORK_DIR (~/obj-evbarm64/work-mouse) with its own disk, so
that it can run beside another session's QEMU.  Log: ~/qemu-mouse.log.

    python3 port/test/usbmouse/qemu-inject.py [-k] [-3] [-s N]

-k keeps the disk image from the last run instead of remaking it.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-mouse")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/usbmouse"
MOUSEEV = GUESTDIR + "/mouseev"


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    r = subprocess.call([XCC, "-O2", "-static", "-Wall", "-Wextra",
                         "--sysroot=" + DEST,
                         "-o", os.path.join(out, "mouseev"),
                         os.path.join(HERE, "mouseev.c")])
    if r != 0:
        sys.exit("build failed: mouseev")


class Guest(object):
    def __init__(self, log, args):
        env = dict(os.environ)
        env["WORK_DIR"] = WORK
        env["QEMU_TIMEOUT"] = "600"
        env["QEMU_SMP"] = str(args.s)
        if not args.k:
            env["DISK_FRESH"] = "1"
        cmd = ["bash", RAMIMAGE, "-d"] + (["-3"] if args.gic3 else [])
        self.log = log
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, bufsize=0,
                                  env=env, start_new_session=True)
        self.buf = ""

    def expect(self, what, timeout):
        deadline = time.time() + timeout
        while True:
            i = self.buf.find(what)
            if i >= 0:
                out = self.buf[:i + len(what)]
                self.buf = self.buf[i + len(what):]
                return out
            if time.time() >= deadline:
                raise RuntimeError("no %r in %.0f s" % (what, timeout))
            if self.p.poll() is not None:
                raise RuntimeError("QEMU exited waiting for %r" % what)
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


SUMMARY = re.compile(r"MOUSE events (-?\d+) x (-?\d+) y (-?\d+) wheel (-?\d+) "
                     r"b1 (\d+)/(\d+) b2 (\d+)/(\d+) b3 (\d+)/(\d+)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-3", dest="gic3", action="store_true", help="GICv3")
    ap.add_argument("-s", type=int, default=1, help="cores")
    a = ap.parse_args()

    if not a.k:
        build()
    os.makedirs(WORK, exist_ok=True)
    log = open(os.path.join(HOME, "qemu-mouse.log"), "w")
    g = Guest(log, a)
    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))

    def run_summary(name, cmd, want, timeout=40):
        """Run cmd, which ends with mouseev's summary; compare it."""
        g.buf = ""
        g.send(cmd + "; printf 'EN%sD\\n' D")
        try:
            out = g.expect("ENDD", timeout)
            g.expect("# ", 20)
        except RuntimeError as e:
            record(name, False, str(e))
            return
        m = SUMMARY.search(out)
        if not m:
            record(name, False, "no summary from mouseev")
            return
        got = tuple(int(v) for v in m.groups())
        if got != want:
            record(name, False, "got %s, want %s" % (got, want))
        else:
            record(name, True)

    # Short lines: tty's canonical buffer cuts a line of 255 bytes.
    def mouse(label, reports, extra=""):
        return "mi %s %s%s" % (label, reports, extra)

    try:
        g.expect("login:", 240)
        g.send("root")
        g.expect("# ", 60)
        g.send("PATH=$PATH:/sbin:/usr/sbin; export PATH")
        g.expect("# ", 20)
        g.send("M=%s; mi() { minix-service up /service/usb_mouse "
               "-label $1 -args \"inject=$2 injectwait=2 $3\"; }" % MOUSEEV)
        g.expect("# ", 20)

        g.send("minix-service up /service/input -dev /dev/kbdmux; "
               "printf 'IN%sUP\\n' PUT")
        try:
            g.expect("INPUTUP", 30)
            g.expect("# ", 20)
            record("the input server starts", True)
        except RuntimeError as e:
            record("the input server starts", False, str(e))

        # Right 10 and up 5, left 3 and down 2, a left click, then a
        # move with the middle button, and the wheel as it comes up.
        # Events: 2 + 2 + 1 + 1 + 3 + 2 = 11.
        reports = "0:10:-5,0:-3:2,1:0:0,0:0:0,4:1:1,0:0:0:3"
        run_summary("moves, clicks and the wheel reach /dev/mousemux",
                    "$M -q -t 6 > /tmp/m1 & %s; wait; cat /tmp/m1; "
                    "minix-service down mousei1"
                    % mouse("mousei1", reports),
                    (11, 8, -2, 3, 1, 1, 0, 0, 1, 1))

        run_summary("wheel=0: the fourth byte is not a wheel",
                    "$M -q -t 5 > /tmp/m2 & %s; wait; cat /tmp/m2; "
                    "minix-service down mousei2"
                    % mouse("mousei2", "0:0:0:5", " wheel=0"),
                    (0, 0, 0, 0, 0, 0, 0, 0, 0, 0))

        # The right button pressed and never released by the "mouse";
        # stopping the driver must release it.
        run_summary("a held button comes up when the driver stops",
                    "$M -q -t 7 > /tmp/m3 & %s; sleep 4; "
                    "minix-service down mousei3; wait; cat /tmp/m3"
                    % mouse("mousei3", "2:0:0"),
                    (2, 0, 0, 0, 0, 0, 1, 1, 0, 0))

        # The input server numbers mice from the first slot it has free;
        # with none of the earlier instances left, this one is mouse0.
        # A device of its own exists only while its driver is there, so
        # the driver first and the reader a second later.
        run_summary("/dev/mouse0 carries the events too",
                    "%s; sleep 1; $M -q -t 4 /dev/mouse0; "
                    "minix-service down mousei4"
                    % mouse("mousei4", "0:7:7"),
                    (2, 7, 7, 0, 0, 0, 0, 0, 0, 0))

        # Keys go to the keyboard side only.
        run_summary("key events do not come out of the mouse device",
                    "$M -q -t 5 > /tmp/m5 & minix-service up "
                    "/service/usb_kbd -label kbdi1 -args "
                    "'inject=true%0a injectwait=2'; wait; cat /tmp/m5; "
                    "minix-service down kbdi1",
                    (0, 0, 0, 0, 0, 0, 0, 0, 0, 0))

        g.send("/sbin/poweroff")
        time.sleep(3)
    finally:
        g.stop()

    bad = [n for n, ok in results if not ok]
    print("\n%d check(s), %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
