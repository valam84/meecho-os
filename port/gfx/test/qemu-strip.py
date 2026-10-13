#!/usr/bin/env python3
"""The strip on QEMU (stage 11.7): meechowm's columns, keys and commands.

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-strip) with
the frame buffer driver at virtual=1, as qemu-meechowm.py does (and stages
DESTDIR/usr/tests/gfx with it: fbart and a font), starts meechowm and opens
three windows of one colour each - wlhello -f, which draws at the size the
compositor gives it: red, green, blue.  Then it steers the strip the way a
person would, with the compositor's keys typed by usb_kbd inject= chords
(%[G+Left] is Super+Left) and a click from usb_mouse inject=, and after
every step checks two things that do not share code:

  - what the compositor says (meechowm -m state): the order of the
    columns, their x and width, which has the focus, which workspace is on
    the screen and how many there are, and that each window took its
    column's size;
  - what is on the screen (fbart, the buffer on display through
    /dev/fb0): the colour in the middle of every column that shows, and
    the focus ring just left of the focused one.

The numbers are worked out by hand for a 1280x720 screen and a gap of 8,
before the run: a half column is 628 wide, two thirds 840; three halves
start on the strip at 0, 636 and 1272.

  1. three windows: the third pushes the view to 636 - red at -628,
     green at 8, blue at 644, blue focused
  2. Super+Left twice: red focused, the view back to 0
  3. Super+r: red two thirds (840), green at 856
  4. Super+Shift+Right: red after green; the view 212, red at 432
  5. Super+c: red in the middle - at 220, 220 + 420 = 640
  6. Super+Shift+Down: red alone on workspace 2 (of 3), at 8
  7. Super+Up: workspace 1 - green at 8, blue at 644, blue focused
  8. the mouse to (300, 200) and a click: green focused
  9. Super+q: green closes, blue is alone at 8
 10. Super+Return: a terminal (foot) opens right of blue, at 644
 11. a command that is not one: an error, and exit status 1
 12. Super+Shift+e: the compositor ends, the console has the screen

At the first failure the tail of the compositor's log is printed.  Log of
the whole run: ~/qemu-strip.log.

    python3 port/gfx/test/qemu-strip.py [-k] [-s cores] [-n] [-x]

-n: no terminal (step 10), which costs two minutes under TCG; -x: stop at
the first failure.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-strip")
GUESTDIR = "/usr/tests/gfx"

RED, GREEN, BLUE = 0xc03030, 0x30c030, 0x3030c0
COLOURS = {"red": RED, "green": GREEN, "blue": BLUE}
BACKGROUND, RING = 0x1d2330, 0x7fc8ff
SCREEN_W, SCREEN_H, GAP = 1280, 720, 8
HALF, TWOTHIRDS = 628, 840
COL_H = SCREEN_H - 2 * GAP

# The stand's staging (fbart, the font) is qemu-meechowm.py's.
_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)

STATE_COL = re.compile(
    r"column (\d+)\.(\d+) (-?\d+) (-?\d+) (\d+) (\d+) width (\S+) (\S+) "
    r"size (\d+)x(\d+) pid (\d+) title (.*)")
STATE_TOP = re.compile(r"screen (\d+)x(\d+) .* gap (\d+) workspaces (\d+) "
                       r"active (\d+)")


def parse_state(text):
    """{'w', 'h', 'gap', 'nws', 'active', 'cols': {ws: [col, ...]}}"""
    st = {"cols": {}}
    for line in text.splitlines():
        m = STATE_TOP.search(line)
        if m:
            st.update(w=int(m.group(1)), h=int(m.group(2)),
                      gap=int(m.group(3)), nws=int(m.group(4)),
                      active=int(m.group(5)))
            continue
        m = STATE_COL.search(line)
        if m:
            st["cols"].setdefault(int(m.group(1)), []).append({
                "x": int(m.group(3)), "y": int(m.group(4)),
                "w": int(m.group(5)), "h": int(m.group(6)),
                "width": m.group(7), "focus": m.group(8) == "focus",
                "sw": int(m.group(9)), "sh": int(m.group(10)),
                "title": m.group(12).strip()})
    return st


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-n", action="store_true", help="no terminal step")
    ap.add_argument("-x", action="store_true",
                    help="stop at the first failure")
    a = ap.parse_args()

    if not a.k:
        qemu_meechowm.stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="3000",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-strip.log"), "w")
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
    dumped = [False]

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()
        if not ok and not dumped[0]:
            # The first failure: what the compositor said up to it.
            dumped[0] = True
            # Without the stand's own polling, which floods it.
            send("grep -v 'command: state' /tmp/mwm.log | tail -40; "
                 "echo LO''GGED")
            out = expect(r"LOGGED", 60)
            print("      /tmp/mwm.log:")
            for l in out.splitlines()[1:-1]:
                print("      | " + l)
            sys.stdout.flush()
            if a.x:
                raise RuntimeError("stopped at the first failure (-x)")

    def state():
        send("meechowm -m state; echo ST''DONE")
        return parse_state(expect(r"STDONE", 120))

    def fbart(args, show=False):
        send("$G/fbart %s; echo FB''DONE" % args)
        out = expect(r"FBDONE", 120)
        m = re.search(r"background 0x([0-9a-f]{6})\b", out)
        k = re.search(r"FBART ink (\d+)\b", out)
        if show:
            for l in out.splitlines():
                if l.startswith("|"):
                    print("      " + l)
        return (int(m.group(1), 16) if m else -1,
                int(k.group(1)) if k else -1)

    def colour_at(x, y, size=32):
        return fbart("-x %d -y %d -w %d -h %d -s %d" %
                     (x - size // 2, y - size // 2, size, size,
                      max(1, size // 4)))[0]

    def wait_for(pred, timeout=150):
        """state() until pred(state) holds; the last state either way."""
        deadline = time.time() + timeout
        while True:
            st = state()
            if pred(st) or time.time() > deadline:
                return st
            time.sleep(2)

    def layout(st, ws):
        return [(c["title"], c["x"], c["w"], c["focus"])
                for c in st["cols"].get(ws, [])]

    def sized(st, ws):
        """Every wlhello of the workspace drew at its column's size."""
        return all(c["sw"] == c["w"] and c["sh"] == c["h"]
                   for c in st["cols"].get(ws, [])
                   if c["title"] in COLOURS)

    def check(step, want, ws=1, active=1, nws=None, timeout=150):
        """The state of workspace ws against want: [(title, x, w, focus)],
        then the screen: each column's colour, the focus ring."""
        st = wait_for(lambda s: s.get("active") == active and
                      layout(s, ws) == want and sized(s, ws), timeout)
        got = layout(st, ws)
        ok = st.get("active") == active and got == want and \
            (nws is None or st.get("nws") == nws)
        record("%s: state" % step, ok,
               "active %s of %s, columns %s" % (st.get("active"),
                                                st.get("nws"), got))
        record("%s: windows took their columns' size" % step,
               sized(st, ws),
               "; ".join("%s %dx%d in %dx%d" % (c["title"], c["sw"],
                                                c["sh"], c["w"], c["h"])
                         for c in st["cols"].get(ws, [])))
        if active != ws:
            return st
        time.sleep(2)
        bad = []
        for title, x, w, focus in want:
            lo, hi = max(x, 0), min(x + w, SCREEN_W)
            if title in COLOURS and hi - lo >= 40:
                c = colour_at((lo + hi) // 2, SCREEN_H // 2)
                if c != COLOURS[title]:
                    bad.append("%s at %d: 0x%06x" % (title, (lo + hi) // 2,
                                                     c))
            if focus and x - 2 >= 0:
                c = fbart("-x %d -y %d -w 2 -h 2 -s 1" %
                          (x - 2, SCREEN_H // 2))[0]
                if c != RING:
                    bad.append("ring left of %s: 0x%06x" % (title, c))
        record("%s: screen" % step, not bad, ", ".join(bad))
        return st

    kbd = [0]

    def keys(chords):
        kbd[0] += 1
        send("minix-service up /service/usb_kbd -label kbdi%d -args "
             "'inject=%s'" % (kbd[0], chords))
        send("sleep 3; minix-service down kbdi%d; echo KE''YS" % kbd[0])
        expect(r"KEYS", 120)

    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("G=%s; mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg"
             % GUESTDIR)
        send("export FONTCONFIG_FILE=$G/fonts.conf")
        send("export MEECHOWM_TERM=\"foot -o main.workers=0 "
             "-f 'DejaVu Sans Mono:size=14'\"")
        send("minix-service up /service/input -dev /dev/kbdmux; "
             "minix-service up /service/fb -dev /dev/fb0 -args virtual=1; "
             "echo UP''DONE")
        expect(r"UPDONE", 60)
        send("MEECHOWM_DEBUG=1 meechowm > /tmp/mwm.log 2>&1 & MWM=$!")
        send("n=0; while [ $n -lt 90 ] && ! meechowm -m state >/dev/null "
             "2>&1; do sleep 2; n=$((n+1)); done; echo WA''ITED $n")
        expect(r"WAITED \d+", 300)
        st = state()
        if (st.get("w"), st.get("h"), st.get("gap")) != \
                (SCREEN_W, SCREEN_H, GAP):
            record("the screen is %dx%d, gap %d" % (SCREEN_W, SCREEN_H, GAP),
                   False, "state says %s" % st)
            raise RuntimeError("the numbers are for another screen")

        # 1. Three windows, one at a time: each opens right of the last.
        for i, (title, argb) in enumerate((("red", "0xffc03030"),
                                           ("green", "0xff30c030"),
                                           ("blue", "0xff3030c0"))):
            send("meechowm -m spawn $G/wlhello -f -t 1800 -c %s -T %s"
                 % (argb, title))
            wait_for(lambda s, n=i + 1: len(s["cols"].get(1, [])) == n, 90)
        check("1. three windows", [("red", -628, HALF, False),
                                   ("green", 8, HALF, False),
                                   ("blue", 644, HALF, True)], nws=2)
        print("      the screen, 1 character for 16x16 pixels:")
        fbart("-w 1280 -h 720 -s 16", show=True)

        keys("%[G+Left]%[G+Left]")
        check("2. Super+Left twice", [("red", 8, HALF, True),
                                      ("green", 644, HALF, False),
                                      ("blue", 1280, HALF, False)])

        keys("%[G+r]")
        check("3. Super+r", [("red", 8, TWOTHIRDS, True),
                             ("green", 856, HALF, False),
                             ("blue", 1492, HALF, False)])

        keys("%[G+S+Right]")
        check("4. Super+Shift+Right", [("green", -204, HALF, False),
                                       ("red", 432, TWOTHIRDS, True),
                                       ("blue", 1280, HALF, False)])

        keys("%[G+c]")
        check("5. Super+c", [("green", -416, HALF, False),
                             ("red", 220, TWOTHIRDS, True),
                             ("blue", 1068, HALF, False)])
        print("      red in the middle:")
        fbart("-w 1280 -h 720 -s 16", show=True)

        keys("%[G+S+Down]")
        check("6. Super+Shift+Down", [("red", 8, TWOTHIRDS, True)],
              ws=2, active=2, nws=3)
        c = colour_at(1064, SCREEN_H // 2)
        record("6. right of red: the desktop", c == BACKGROUND,
               "0x%06x" % c)

        keys("%[G+Up]")
        check("7. Super+Up", [("green", 8, HALF, False),
                              ("blue", 644, HALF, True)], nws=3)

        # Into the top left corner first, wherever the cursor was, then
        # 300 right and 200 down - over green - and a click.
        send("minix-service up /service/usb_mouse -label mousei1 -args "
             "'inject=%s,0:100:0,0:100:0,0:100:0,0:0:100,0:0:100,"
             "1:0:0,0:0:0'" % ",".join(["0:-127:-127"] * 12))
        send("sleep 8; minix-service down mousei1; "
             "grep 'cursor at' /tmp/mwm.log | tail -1; echo MO''VED")
        out = expect(r"MOVED", 120)
        m = re.findall(r"cursor at (\d+),(\d+)", out)
        print("      cursor at %s" % (",".join(m[-1]) if m else "?"))
        check("8. a click on green", [("green", 8, HALF, True),
                                      ("blue", 644, HALF, False)])

        keys("%[G+q]")
        check("9. Super+q", [("blue", 8, HALF, True)])

        if not a.n:
            keys("%[G+Return]")
            st = wait_for(lambda s: len(s["cols"].get(1, [])) == 2 and
                          s["cols"][1][1]["sw"] == s["cols"][1][1]["w"],
                          240)
            cols = st["cols"].get(1, [])
            ok = len(cols) == 2 and cols[1]["x"] == 644 and \
                cols[1]["focus"] and cols[1]["title"] != "-"
            record("10. Super+Return: a terminal right of blue", ok,
                   "columns %s" % layout(st, 1))
            if len(cols) == 2:
                record("10. it took its column's size",
                       cols[1]["sw"] == cols[1]["w"] and
                       cols[1]["sh"] == cols[1]["h"],
                       "%dx%d in %dx%d" % (cols[1]["sw"], cols[1]["sh"],
                                           cols[1]["w"], cols[1]["h"]))
                time.sleep(3)
                bg, ink = fbart("-x 644 -y 8 -w 320 -h 64 -s 2", show=True)
                record("10. its column carries text", bg not in
                       (BACKGROUND, BLUE) and ink > 100,
                       "background 0x%06x, %d pixel(s) of ink" % (bg, ink))

        send("meechowm -m frobnicate; echo RC=$?")
        out = expect(r"RC=\d+", 60)
        record("11. not a command: an error, status 1",
               "error:" in out and out.rstrip().endswith("RC=1"),
               out.strip().splitlines()[-1] if out.strip() else "")

        keys("%[G+S+e]")
        send("sleep 4; if kill -0 $MWM 2>/dev/null; then echo AL''IVE; "
             "else echo GO''NE; fi")
        out = expect(r"ALIVE|GONE", 60)
        record("12. Super+Shift+e ends the compositor", "GONE" in out,
               "it is still running")
        c = colour_at(320, SCREEN_H // 2)
        record("12. the console has the screen back",
               c not in (BACKGROUND, RED, GREEN, BLUE), "0x%06x" % c)
        send("grep -E 'ERROR|command:' /tmp/mwm.log | tail -20")
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
