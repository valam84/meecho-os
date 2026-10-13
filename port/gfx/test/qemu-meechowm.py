#!/usr/bin/env python3
"""meechowm on QEMU (stage 11.4): the compositor on the frame buffer.

Stages ~/gfx-build/bin (meechowm, wlhello, foot, fbart), the XKB data, a
font (DejaVu Sans Mono) and a fonts.conf into DESTDIR/usr/tests/gfx, boots
the disk root in its own WORK_DIR (~/obj-evbarm64/work-mwm), and then, with
no display controller - the frame buffer driver started with virtual=1,
the scanout buffers only - looks at the screen through /dev/fb0 (fbart,
which reads the buffer on display):

  1. meechowm takes the screen and starts wlhello: a 256x256 window of
     one colour must be in the top left corner;
  2. a mouse (usb_mouse inject=) moves 300 right and 200 down: where the
     compositor says the cursor went (its debug log), the screen must carry
     the cursor, on the desktop's background where there was none before;
  3. foot, a second client, opens a terminal that prints FOOT-OK: its
     window must carry text - ink on its background;
  4. meechowm is stopped: the console must have the screen back.

The whole screen, scaled down, is printed after each step.  Log:
~/qemu-meechowm.log.

    python3 port/gfx/test/qemu-meechowm.py [-k] [-s cores]
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
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.join(HOME, "obj-evbarm64/work-mwm")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
BIN = os.path.join(HOME, "gfx-build/bin")
XKB = os.path.join(HOME, "gfx-sysroot/usr/share/X11/xkb")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
GUESTDIR = "/usr/tests/gfx"
COLOUR = 0x3060c0


def stage():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    for prog in ("meechowm", "wlhello", "foot", "fbart"):
        shutil.copy2(os.path.join(BIN, prog), out)
    xkb = os.path.join(out, "xkb")
    if os.path.exists(xkb):
        shutil.rmtree(xkb)
    shutil.copytree(XKB, xkb, symlinks=False)
    fonts = os.path.join(out, "fonts")
    os.makedirs(fonts, exist_ok=True)
    shutil.copy2(FONT, fonts)
    with open(os.path.join(out, "fonts.conf"), "w") as f:
        f.write('<?xml version="1.0"?>\n<fontconfig>\n'
                '  <dir>%s/fonts</dir>\n  <cachedir>/tmp/fc</cachedir>\n'
                '</fontconfig>\n' % GUESTDIR)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    a = ap.parse_args()

    if not a.k:
        stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="2400",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-meechowm.log"), "w")
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

    def fbart(args, show=False):
        """fbart on the guest: (background, ink, picture lines)."""
        send("$G/fbart %s; echo FB''DONE" % args)
        out = expect(r"FBDONE", 120)
        m = re.search(r"background 0x([0-9a-f]{6})\b", out)
        k = re.search(r"FBART ink (\d+)\b", out)
        pic = [l for l in out.splitlines() if l.startswith("|")]
        if show:
            for l in pic:
                print("      " + l)
        return (int(m.group(1), 16) if m else -1,
                int(k.group(1)) if k else -1, pic)

    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("G=%s; mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg"
             % GUESTDIR)
        send("export XKB_CONFIG_ROOT=$G/xkb FONTCONFIG_FILE=$G/fonts.conf")
        send("minix-service up /service/input -dev /dev/kbdmux; "
             "minix-service up /service/fb -dev /dev/fb0 -args virtual=1; "
             "echo UP''DONE")
        expect(r"UPDONE", 60)

        # By pid, not %1: ash's kill %N signals the job's process group,
        # and PM has no groups apart from sessions - "No such process".
        send("MEECHOWM_DEBUG=1 $G/meechowm -s \"$G/wlhello -t 900\" "
             "> /tmp/mwm.log 2>&1 & MWM=$!")
        # Until a frame has the window's rows in it.
        send("n=0; while [ $n -lt 150 ] && ! grep -q 'rows 0..256' "
             "/tmp/mwm.log; do sleep 2; n=$((n+1)); done; sleep 4; "
             "echo WA''ITED $n")
        expect(r"WAITED \d+", 400)
        print("      the screen, 1 character for 16x16 pixels:")
        whole1 = fbart("-w 1280 -h 720 -s 16", show=True)[2]
        bg, ink, _ = fbart("-x 96 -y 96 -w 64 -h 64 -s 8")
        record("1. the window is in the top left corner (0x%06x)" % COLOUR,
               bg == COLOUR, "the area's colour is 0x%06x" % bg)

        send("minix-service up /service/usb_mouse -label mousei1 -args "
             "'inject=0:100:0,0:100:0,0:100:0,0:0:100,0:0:100 "
             "injectwait=2'")
        send("sleep 20; grep 'cursor at' /tmp/mwm.log | tail -1; "
             "echo MO''VED")
        out = expect(r"MOVED", 120)
        m = re.findall(r"cursor at (\d+),(\d+)", out)
        if not m:
            record("2. the cursor moves", False, "no motion in the log")
        else:
            cx, cy = int(m[-1][0]), int(m[-1][1])
            # Before: nothing but the desktop there (a 16x16 cell of the
            # whole-screen picture, blank); now the cursor's pixels.
            row = whole1[cy // 16] if cy // 16 < len(whole1) else ""
            was = row[1 + cx // 16] if 1 + cx // 16 < len(row) else "?"
            bg, ink, _ = fbart("-x %d -y %d -w 24 -h 24 -s 1" % (cx, cy),
                               show=True)
            record("2. the cursor is drawn where it went (%d,%d)" % (cx, cy),
                   was == " " and ink >= 10,
                   "before %r, now %d pixel(s) of it" % (was, ink))

        send("$G/foot -o main.workers=0 -f 'DejaVu Sans Mono:size=14' "
             "-e /bin/sh -c 'echo FOOT-OK; sleep 900' > /tmp/foot.log 2>&1 &")
        send("sleep 120; echo FO''OTED")
        expect(r"FOOTED", 400)
        send("grep -v -E 'compositor does not implement|no configuration' "
             "/tmp/foot.log | head -12; ls -l /dev/pts")
        print("      the screen after foot:")
        fbart("-w 1280 -h 720 -s 16", show=True)
        print("      its top left, every other pixel:")
        bg, ink, _ = fbart("-x 0 -y 0 -w 320 -h 48 -s 2", show=True)
        record("3. foot's window carries text", bg != COLOUR and ink > 200,
               "background 0x%06x, %d pixel(s) of ink" % (bg, ink))

        send("grep -E 'frame [0-9]+:' /tmp/mwm.log | head -12; "
             "grep -E 'INFO|ERROR' /tmp/mwm.log | tail -8")
        send("kill $MWM; sleep 8; echo ST''OPPED")
        expect(r"STOPPED", 120)
        bg, ink, _ = fbart("-x 96 -y 96 -w 64 -h 64 -s 8")
        record("4. stopped: the window is gone from the screen",
               bg != COLOUR, "the area is still 0x%06x" % bg)
        send("minix-service down mousei1; /sbin/poweroff")
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
