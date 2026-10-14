#!/usr/bin/env python3
"""The overview of meechowm on QEMU (stage 11.7): the camera pulled back.

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-overview)
with the frame buffer driver at virtual=1, as qemu-strip.py does, and
opens three windows of one colour each (wlhello -f): red and green, two
halves on workspace 1, then blue, moved to workspace 2; back on 1, green
has the focus.  Every check is against what the compositor says (meechowm
-m state) and against the screen (fbart through /dev/fb0), the points
worked out by hand for 1280x720 and a gap of 8, before the run: at half
size about (640, 360) a box (x, y, w, h) goes to (640 + (x - 640) / 2,
360 + (y - 360) / 2) and half its size -

  red   (8, 8, 628, 704)        to x 324..638, y 184..536: red at 481,360
  green (644, 8, 628, 704)      to x 642..956: green at 799,360, and the
                                focus ring left of it, at 640..641
  blue  (8, 728, 628, 704)      the workspace below, to y 544..896: blue
                                at 481,630

  1. Super+o, the move stretched to 12 s: on its way, the camera strictly
     between full and half size;
  2. there: zoom 500, and the three colours and the ring where they go;
     outside every card, the desktop;
  3. the arrows alone (Left): red has the focus, and the ring is left of
     red now (at 322);
  4. Return: back on the strip, at full size - red at 322,360, as before;
  5. Super+o again and a click on green's picture: green has the focus,
     and the overview is gone;
  6. "overview" by command, then Escape: gone again; the compositor ends
     cleanly.

-c PATH: the compositor at PATH in the guest instead of /usr/bin/meechowm.
Log: ~/qemu-overview.log.

    python3 port/gfx/test/qemu-overview.py [-k] [-s cores] [-c PATH]
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
WORK = os.path.join(HOME, "obj-evbarm64/work-overview")
GUESTDIR = "/usr/tests/gfx"

RED, GREEN, BLUE = 0xc03030, 0x30c030, 0x3030c0
INK, SURFACE, ACCENT = 0x1d2330, 0x283142, 0x7fc8ff


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


qemu_meechowm = load("qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_strip = load("qemu_strip", os.path.join(HERE, "qemu-strip.py"))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-c", default="meechowm", help="the compositor")
    a = ap.parse_args()

    if not a.k:
        qemu_meechowm.stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="2400",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-overview.log"), "w")
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
        out = expect(r"RUNDONE", timeout)
        return "\n".join(out.splitlines()[1:-1])

    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    def state():
        text = run("meechowm -m state")
        st = qemu_strip.parse_state(text)
        m = re.search(r"overview (\d) zoom (\d+)", text)
        st["overview"] = int(m.group(1)) if m else -1
        st["zoom"] = int(m.group(2)) if m else -1
        return st

    def wait_for(pred, timeout=150):
        deadline = time.time() + timeout
        while True:
            st = state()
            if pred(st) or time.time() > deadline:
                return st
            time.sleep(2)

    def colour(x, y, size=8):
        out = run("$G/fbart -x %d -y %d -w %d -h %d -s 1" %
                  (x - size // 2, y - size // 2, size, size))
        m = re.search(r"background 0x([0-9a-f]{6})\b", out)
        return int(m.group(1), 16) if m else -1

    def focused(st):
        """The focused column of the workspace on the screen (every
        workspace has one of its own)."""
        return [c["title"] for c in st["cols"].get(st.get("active"), [])
                if c["focus"]]

    kbd = [0]

    def keys(chords):
        kbd[0] += 1
        send("minix-service up /service/usb_kbd -label kbdi%d -args "
             "'inject=%s'" % (kbd[0], chords))
        run("sleep 3; minix-service down kbdi%d" % kbd[0])

    def spawn(title, argb, n):
        send("meechowm -m spawn $G/wlhello -f -t 1800 -c %s -T %s"
             % (argb, title))
        wait_for(lambda s: sum(len(c) for c in s["cols"].values()) == n, 90)

    def screen(step, points):
        """points: (name, x, y, colour, size) - a size x size square
        centred at x, y, all of one colour."""
        bad = []
        for name, x, y, want, size in points:
            c = colour(x, y, size)
            if c != want:
                bad.append("%s at %d,%d: 0x%06x" % (name, x, y, c))
        record("%s: the screen" % step, not bad, ", ".join(bad))

    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("G=%s; mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg"
             % GUESTDIR)
        send("export FONTCONFIG_FILE=$G/fonts.conf")
        send("minix-service up /service/input -dev /dev/kbdmux; "
             "minix-service up /service/fb -dev /dev/fb0 -args virtual=1; "
             "echo UP''DONE")
        expect(r"UPDONE", 60)
        send("MEECHOWM_DEBUG=1 %s > /tmp/mwm.log 2>&1 & MWM=$!" % a.c)
        send("n=0; while [ $n -lt 90 ] && ! meechowm -m state >/dev/null "
             "2>&1; do sleep 2; n=$((n+1)); done; echo WA''ITED $n")
        expect(r"WAITED \d+", 300)
        run("meechowm -m anim 0")
        spawn("red", "0xffc03030", 1)
        spawn("blue", "0xff3030c0", 2)
        run("meechowm -m move down; meechowm -m workspace 1")
        spawn("green", "0xff30c030", 3)
        st = wait_for(lambda s: s.get("active") == 1 and
                      focused(s) == ["green"] and
                      [c["title"] for c in s["cols"].get(1, [])] ==
                      ["red", "green"], 60)
        record("the windows: red, green on 1, green focused; blue on 2",
               [(c["title"], c["x"]) for c in st["cols"].get(1, [])] ==
               [("red", 8), ("green", 644)] and
               [c["title"] for c in st["cols"].get(2, [])] == ["blue"] and
               focused(st) == ["green"],
               "columns %s" % st["cols"])

        # 1. On its way.
        run("meechowm -m anim 12000")
        keys("%[G+o]")
        st = state()
        record("1. Super+o: the camera on its way",
               st["overview"] == 1 and 500 < st["zoom"] < 1000,
               "overview %d, zoom %d" % (st["overview"], st["zoom"]))

        # 2. There.
        st = wait_for(lambda s: s["zoom"] == 500 and s.get("animating") == 0,
                      60)
        run("meechowm -m anim 0")
        record("2. the overview: at half size", st["zoom"] == 500 and
               st["overview"] == 1, "zoom %d" % st["zoom"])
        # Red covers 324..637, green 642..955: between them ws 1's card
        # (638, 639) and the ring left of green (640, 641).
        screen("2", [("red", 481, 360, RED, 8), ("green", 799, 360, GREEN, 8),
                     ("blue", 481, 630, BLUE, 8),
                     ("ws 1's card", 639, 360, SURFACE, 2),
                     ("the ring", 641, 360, ACCENT, 2),
                     ("no card", 1100, 360, INK, 8)])

        # 3. The arrows alone.
        keys("%[Left]")
        st = wait_for(lambda s: focused(s) == ["red"], 30)
        record("3. Left: red has the focus", focused(st) == ["red"] and
               st["overview"] == 1, "focus %s, overview %d" %
               (focused(st), st["overview"]))
        screen("3", [("the ring left of red", 323, 360, ACCENT, 2),
                     ("none left of green", 641, 360, SURFACE, 2)])

        # 4. Return.
        keys("%[Return]")
        st = wait_for(lambda s: s["overview"] == 0 and s["zoom"] == 1000, 30)
        record("4. Return: back on the strip, full size",
               st["overview"] == 0 and st["zoom"] == 1000 and
               focused(st) == ["red"], "overview %d zoom %d focus %s" %
               (st["overview"], st["zoom"], focused(st)))
        screen("4", [("red", 322, 360, RED, 8), ("green", 958, 360, GREEN, 8)])

        # 5. A click on green's picture.
        keys("%[G+o]")
        wait_for(lambda s: s["zoom"] == 500, 30)
        send("minix-service up /service/usb_mouse -label mousei1 -args "
             "'inject=%s,0:127:0,0:127:0,0:127:0,0:127:0,0:127:0,0:127:0,"
             "0:37:0,0:0:120,0:0:120,0:0:120,1:0:0,0:0:0'" %
             ",".join(["0:-127:-127"] * 12))
        run("sleep 8; minix-service down mousei1")
        st = wait_for(lambda s: s["overview"] == 0, 30)
        out = run("grep 'cursor at' /tmp/mwm.log | tail -1")
        record("5. a click on green's picture: green, and the strip",
               st["overview"] == 0 and focused(st) == ["green"],
               "overview %d focus %s; %s" % (st["overview"], focused(st),
                                             out.strip()))

        # 6. By command, then Escape.
        run("meechowm -m overview")
        st = wait_for(lambda s: s["overview"] == 1, 30)
        keys("%[Escape]")
        st = wait_for(lambda s: s["overview"] == 0 and s["zoom"] == 1000, 30)
        record("6. 'overview', then Escape: gone again",
               st["overview"] == 0 and st["zoom"] == 1000,
               "overview %d zoom %d" % (st["overview"], st["zoom"]))
        out = run("meechowm -m overview sideways; echo RC=$?")
        record("6. 'overview sideways': an error", "RC=1" in out,
               out.strip())

        run("kill $MWM; sleep 6")
        out = run("echo AB=$(grep -cE 'abort|Assertion|assertion' "
                  "/tmp/mwm.log)")
        record("the compositor ends cleanly", "AB=0" in out, out.strip())
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
