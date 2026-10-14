#!/usr/bin/env python3
"""The cursor in the display controller's window, on QEMU (stage 11.7).

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-hwcursor)
with the frame buffer driver at virtual=1, as qemu-meechowm.py does (and
with its staging: fbart, a font), and runs meechowm with wlhello twice in
one boot - the cursor drawn into the frames (MEECHOWM_SOFT_CURSOR=1), then
in the driver's window - and the same moves of the mouse each time: four
of them (usb_mouse inject=), seconds apart, so that each falls into a
frame of its own, all on the desktop outside the window.  For each:

  - what the moves cost the compositor, from its debug log: frames shown
    (FBIOC_FLIP) and cursor updates (FBIOC_CURSOR) while only the pointer
    moved - drawn into the frames, a frame a move and no update; in the
    window, no frame and an update a move;
  - where the compositor says the cursor went, the screen (fbart, which
    reads /dev/fb0 - the frame with the driver's cursor blended in) must
    carry it, on the desktop's background;
  - in the window: the driver's report (fblog) has the cursor there, and
    hidden once the compositor has gone.

-c PATH: the compositor at PATH in the guest instead of /usr/bin/meechowm.
Log: ~/qemu-hwcursor.log.

    python3 port/gfx/test/qemu-hwcursor.py [-k] [-s cores] [-c PATH]
"""

import argparse
import importlib.util
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
WORK = os.path.join(HOME, "obj-evbarm64/work-hwcursor")
GUESTDIR = "/usr/tests/gfx"
FBTESTDIR = "/usr/tests/fb"
BACKGROUND = 0x1d2330
# The moves: out of wlhello's window first - over a window that has not
# set a cursor there is no cursor at all, nothing to draw or to move - then
# along the desktop: from 100,100 to 400,350, 460,370, 520,390, 580,410.
MOVE_REPORTS = ["0:100:0,0:100:0,0:100:0,0:0:125,0:0:125",
                "0:60:20", "0:60:20", "0:60:20"]
MOVES = len(MOVE_REPORTS)

_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-c", default="/usr/bin/meechowm",
                    help="the compositor (guest path)")
    ap.add_argument("-w", action="store_true",
                    help="the driver's window only")
    a = ap.parse_args()

    if not a.k:
        qemu_meechowm.stage()
        fbdir = os.path.join(qemu_meechowm.DEST, FBTESTDIR.lstrip("/"))
        os.makedirs(fbdir, exist_ok=True)
        if subprocess.call([qemu_meechowm.XCC, "-O2", "-static", "-Wall",
                            "--sysroot=" + qemu_meechowm.DEST, "-I",
                            os.path.join(HERE, "..", "..", "..", "minix",
                                         "drivers/video/fb/arch/aarch64"),
                            "-o", os.path.join(fbdir, "fblog"),
                            os.path.join(HERE, "..", "..", "test", "fb",
                                         "fblog.c")]) != 0:
            sys.exit("build failed: fblog")
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="2400",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-hwcursor.log"), "w")
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

    def run(cmd, timeout=120):
        send("%s; echo RU''NDONE" % cmd)
        return expect(r"RUNDONE", timeout)

    def number(cmd, tag):
        out = run("echo %s=$(%s)" % (tag, cmd))
        m = re.search(r"%s=(\d+)" % tag, out.split("\n", 1)[-1])
        return int(m.group(1)) if m else -1

    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    def fbart(args):
        out = run("$G/fbart %s" % args)
        m = re.search(r"background 0x([0-9a-f]{6})\b", out)
        k = re.search(r"FBART ink (\d+)\b", out)
        return (int(m.group(1), 16) if m else -1,
                int(k.group(1)) if k else -1)

    mice = [0]

    def one(soft, tag):
        send("%sMEECHOWM_DEBUG=1 %s -s \"$G/wlhello -t 900\" > /tmp/mwm.log "
             "2>&1 & MWM=$!" % ("MEECHOWM_SOFT_CURSOR=1 " if soft else "",
                                a.c))
        send("n=0; while [ $n -lt 150 ] && ! grep -q 'rows 0..256' "
             "/tmp/mwm.log; do sleep 2; n=$((n+1)); done; echo WA''ITED $n")
        expect(r"WAITED \d+", 400)
        time.sleep(5)
        drawn = number("grep -c 'cursor is drawn into the frames' "
                       "/tmp/mwm.log", "SW")
        record("%s: the compositor says so" % tag, (drawn > 0) == soft,
               "%d line(s) 'drawn into the frames'" % drawn)
        f0 = number("grep -c 'frame [0-9]*: buffer' /tmp/mwm.log", "F0")
        c0 = number("grep -c 'hardware cursor' /tmp/mwm.log", "C0")
        for reports in MOVE_REPORTS:
            mice[0] += 1
            run("minix-service up /service/usb_mouse -label mousei%d -args "
                "'inject=%s injectwait=1'; sleep 4; "
                "minix-service down mousei%d" % (mice[0], reports, mice[0]))
        f1 = number("grep -c 'frame [0-9]*: buffer' /tmp/mwm.log", "F1")
        c1 = number("grep -c 'hardware cursor' /tmp/mwm.log", "C1")
        frames, updates = f1 - f0, c1 - c0
        if soft:
            record("%s: a frame a move, no cursor update" % tag,
                   frames >= MOVES and updates == 0,
                   "%d frame(s), %d update(s) for %d move(s)" %
                   (frames, updates, MOVES))
        else:
            record("%s: no frame, an update a move" % tag,
                   frames == 0 and updates >= MOVES,
                   "%d frame(s), %d update(s) for %d move(s)" %
                   (frames, updates, MOVES))
        if results[-1][1] is False:
            out = run("grep -E 'cursor at|hardware cursor|frame [0-9]+:' "
                      "/tmp/mwm.log | tail -24")
            print("      /tmp/mwm.log:")
            for line in out.splitlines()[1:-1]:
                print("      | " + line.rstrip())
        out = run("grep 'cursor at' /tmp/mwm.log | tail -1")
        m = re.findall(r"cursor at (\d+),(\d+)", out)
        if not m:
            record("%s: the cursor moves" % tag, False, "not in the log")
        else:
            cx, cy = int(m[-1][0]), int(m[-1][1])
            bg, ink = fbart("-x %d -y %d -w 24 -h 24 -s 1" % (cx, cy))
            record("%s: the cursor is on the screen where it went (%d,%d)"
                   % (tag, cx, cy), bg == BACKGROUND and ink > 0,
                   "background 0x%06x, %d pixel(s) of ink" % (bg, ink))
            if not soft:
                out = run("$F/fblog | grep 'cursor: ' | tail -1")
                fm = re.search(r"cursor: at (-?\d+),(-?\d+), (\d+)x(\d+)",
                               out)
                record("%s: the driver has it there" % tag,
                       fm is not None and
                       abs(int(fm.group(1)) - cx) < 32 and
                       abs(int(fm.group(2)) - cy) < 32,
                       out.strip().splitlines()[-1] if out.strip() else "")
        run("kill $MWM; sleep 8")
        out = run("$F/fblog | grep 'cursor: ' | tail -1")
        record("%s: no cursor once the compositor has gone" % tag,
               "cursor: hidden" in out, out.strip().splitlines()[-1]
               if out.strip() else "")
        print("      %s: %d frame(s) shown, %d cursor update(s) for %d "
              "move(s)" % (tag, frames, updates, MOVES))

    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("G=%s; F=%s; mkdir -p -m 700 /tmp/xdg; "
             "export XDG_RUNTIME_DIR=/tmp/xdg" % (GUESTDIR, FBTESTDIR))
        send("export FONTCONFIG_FILE=$G/fonts.conf")
        send("minix-service up /service/input -dev /dev/kbdmux; "
             "minix-service up /service/fb -dev /dev/fb0 -args virtual=1; "
             "echo UP''DONE")
        expect(r"UPDONE", 60)
        if not a.w:
            one(True, "drawn into the frames")
        one(False, "in the driver's window")
        send("/sbin/poweroff")
        time.sleep(5)
    except RuntimeError as e:
        record("the run", False, str(e))
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    bad = [n for n, ok in results if not ok]
    print("\n%d check(s), %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
