#!/usr/bin/env python3
"""The screen's mode changed while the compositor runs, on QEMU.

The frame buffer driver at virtual=1 lists four modes of its own (1280x720,
1920x1080, 1024x768, 800x600 at 60 Hz), so the change can be made with no
display controller.  The stand sets a desktop up - meechowm with its panel
(meechowm-bar: yambar) and two windows of one colour each, blue and red,
half a screen wide - and then:

  1. meechowm -m modes: the four, 1280x720 current
  2. meechowm -m mode W x H for 1024x768, 1920x1080, 800x600 and back to
     1280x720, each against
       - the compositor's state: the screen, the panel across the new
         width, the area under it, both columns at (area - gap) / 2 - gap
         and the windows at their columns' size, nothing moving;
       - the screen itself, read back through /dev/fb0 (fbart): the frame's
         geometry, and blue and red in the middles of their columns
  3. a mode the screen does not have and one that is not a mode at all:
     an error, exit status 1, the screen as it was; fbmode(8) from the
     console while the compositor has the screen: refused, status 1
  4. 1024x768, then the compositor quits: the console has the screen at
     that size, with text on it (fbcon_resize())
  5. fbmode from the console, nobody owning the screen: the list with
     1024x768 current; 1920x1080, 800x600 and 1280x720, each the frame's
     geometry and the console's text drawn in it; a mode the screen does
     not have (status 1) and a word that is not a mode (status 2)
  6. a monitor plugged in under the compositor (before 4; FBIOC_VPLUG,
     port/test/fb/fbvplug.c): one with 1024x768 and 800x600 only - the
     compositor takes 1024x768 by itself, the list is that monitor's, the
     windows are in their places; taken out - the mode stays; the first
     monitor back - 1280x720 again
  7. the same at the console (after 5): the driver changes the mode by
     itself, the console's text in the new frame; the same monitor out
     and back in - the mode stays

    python3 port/gfx/test/qemu-mode.py [-x]

-x: stop at the first failure.  Log of the run: ~/qemu-mode.log.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-mode")
GUESTDIR = "/usr/tests/gfx"
GAP = 8
PANEL = 24
BLUE = 0x3a7bd5
RED = 0xd5453a
MODES = ["1280x720@60 current", "1920x1080@60", "1024x768@60",
         "800x600@60"]

_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)

checks = failed = 0
stop_at_failure = False


class Failed(Exception):
    pass


def record(name, ok, detail=""):
    global checks, failed
    checks += 1
    if not ok:
        failed += 1
    print("%-5s %s%s" % ("ok" if ok else "FAIL", name,
                         ": " + detail if detail and not ok else ""))
    sys.stdout.flush()
    if not ok and stop_at_failure:
        raise Failed(name)


def half(area_w):
    return (area_w - GAP) * 1 // 2 - GAP


def main():
    global stop_at_failure
    ap = argparse.ArgumentParser()
    ap.add_argument("-x", action="store_true", help="stop at a failure")
    a = ap.parse_args()
    stop_at_failure = a.x

    qemu_meechowm.stage()
    if subprocess.call([qemu_meechowm.XCC, "-O2", "-static", "-Wall",
                        "-Wextra", "--sysroot=" + qemu_meechowm.DEST,
                        "-I" + os.path.join(HERE, "..", "..", "..", "minix",
                                            "drivers", "video", "fb", "arch",
                                            "aarch64"),
                        "-o", os.path.join(qemu_meechowm.DEST,
                                           GUESTDIR.lstrip("/"), "fbvplug"),
                        os.path.join(HERE, "..", "..", "test", "fb",
                                     "fbvplug.c")]) != 0:
        sys.exit("build failed: fbvplug")
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="2400",
               QEMU_SMP="1", DISK_FRESH="1")
    log = open(os.path.join(HOME, "qemu-mode.log"), "w")
    p = subprocess.Popen(["bash", RAMIMAGE, "-d"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         bufsize=0, env=env, start_new_session=True)
    buf = ""

    def expect(pat, timeout):
        nonlocal buf
        deadline = time.time() + timeout
        while True:
            m = re.search(pat, buf, re.S)
            if m:
                out = buf[:m.end()]
                buf = buf[m.end():]
                return out
            if time.time() > deadline or p.poll() is not None:
                raise Failed("no %r" % pat)
            if select.select([p.stdout], [], [], 0.2)[0]:
                c = p.stdout.read(4096).decode(errors="replace")
                buf += c
                log.write(c)
                log.flush()

    def send(line):
        p.stdin.write((line + "\n").encode())
        p.stdin.flush()
        time.sleep(0.3)

    def run(cmd, timeout=120):
        """A command's output, up to an end mark the echo cannot match."""
        send("%s; echo E''ND=$?" % cmd)
        out = expect(r"END=\d+", timeout)
        out = out.replace("\r", "")
        lines = out.split("\n")
        # The echo of the command is the first line.
        return "\n".join(lines[1:])

    def state():
        out = run("meechowm -m state")
        st = {"cols": [], "layers": []}
        m = re.search(r"screen (\d+)x(\d+) area (\d+),(\d+) (\d+)x(\d+) "
                      r"gap \d+ workspaces \d+ active \d+ animating (\d)",
                      out)
        if m:
            st["screen"] = (int(m.group(1)), int(m.group(2)))
            st["area"] = tuple(int(m.group(i)) for i in range(3, 7))
            st["animating"] = int(m.group(7))
        for m in re.finditer(r"column 1\.\d+ (-?\d+) (-?\d+) (\d+) (\d+) "
                             r"width (\S+) \S+ at (-?\d+) size (\d+)x(\d+) "
                             r"pid \d+ title (\S+)", out):
            st["cols"].append({"x": int(m.group(1)), "y": int(m.group(2)),
                               "w": int(m.group(3)), "h": int(m.group(4)),
                               "sw": int(m.group(7)), "sh": int(m.group(8)),
                               "title": m.group(9)})
        for m in re.finditer(r"layer (\S+) \d+ (-?\d+) (-?\d+) (\d+) (\d+)",
                             out):
            st["layers"].append({"ns": m.group(1), "x": int(m.group(2)),
                                 "y": int(m.group(3)), "w": int(m.group(4)),
                                 "h": int(m.group(5))})
        return st

    def wait_for(cond, timeout):
        deadline = time.time() + timeout
        st = state()
        while not cond(st) and time.time() < deadline:
            time.sleep(2)
            st = state()
        return st

    def fbart(args):
        out = run("%s/fbart %s" % (GUESTDIR, args))
        g = re.search(r"FBART (\d+)x(\d+) stride (\d+)", out)
        bg = re.search(r"background 0x([0-9a-f]{6})", out)
        ink = re.search(r"FBART ink (\d+)", out)
        return ((int(g.group(1)), int(g.group(2)), int(g.group(3)))
                if g else None,
                int(bg.group(1), 16) if bg else -1,
                int(ink.group(1)) if ink else -1)

    rc = 1
    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("G=%s; mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg"
             % GUESTDIR)
        send("export FONTCONFIG_FILE=$G/fonts.conf MEECHOWM_ANIM_MS=0")
        run("minix-service up /service/input -dev /dev/kbdmux; "
            "minix-service up /service/fb -dev /dev/fb0 -args virtual=1")
        send("meechowm > /tmp/mwm.log 2>&1 &")
        run("n=0; while [ $n -lt 90 ] && ! meechowm -m state >/dev/null "
            "2>&1; do sleep 2; n=$((n+1)); done", 300)
        run("meechowm -m spawn meechowm-bar")
        run("meechowm -m spawn $G/wlhello -f -t 3600 -c 0xff3a7bd5 -T blue")
        wait_for(lambda s: len(s["cols"]) == 1, 300)
        run("meechowm -m spawn $G/wlhello -f -t 3600 -c 0xffd5453a -T red")

        def settled(s, w, h):
            cols = s["cols"]
            return s.get("screen") == (w, h) and s.get("animating") == 0 and \
                any(l["ns"] == "panel" and l["w"] == w
                    for l in s["layers"]) and len(cols) == 2 and \
                all(c["sw"] == c["w"] and c["sh"] == c["h"] for c in cols)

        st = wait_for(lambda s: settled(s, 1280, 720), 400)
        record("0. the desktop at 1280x720: panel, blue and red",
               settled(st, 1280, 720), "%s" % st)

        out = run("meechowm -m modes").strip().split("\n")
        got = [l.strip() for l in out if re.match(r"\d+x\d+@\d+", l.strip())]
        record("1. modes: the virtual display's four, 1280x720 current",
               got == MODES, "%s" % got)

        for w, h in ((1024, 768), (1920, 1080), (800, 600), (1280, 720)):
            out = run("meechowm -m mode %dx%d" % (w, h))
            record("2. mode %dx%d: ok" % (w, h),
                   "END=0" in out and "error" not in out, out.strip())
            st = wait_for(lambda s: settled(s, w, h), 120)
            area = (0, PANEL, w, h - PANEL)
            cw = half(w)
            cols = st["cols"]
            record("2. mode %dx%d: the state" % (w, h),
                   settled(st, w, h) and st.get("area") == area and
                   [c["w"] for c in cols] == [cw, cw] and
                   [c["x"] for c in cols] == [GAP, GAP + cw + GAP] and
                   all(c["y"] == PANEL + GAP and
                       c["h"] == h - PANEL - 2 * GAP for c in cols),
                   "%s" % st)
            time.sleep(2)
            geo = fbart("-w 1 -h 1")[0]
            record("2. mode %dx%d: the frame" % (w, h),
                   geo == (w, h, w * 4), "%s" % (geo,))
            for c, colour, name in ((0, BLUE, "blue"), (1, RED, "red")):
                if len(cols) < 2:
                    break
                x = cols[c]["x"] + cols[c]["w"] // 2
                y = cols[c]["y"] + cols[c]["h"] // 2
                bg = fbart("-x %d -y %d -w 4 -h 4 -s 1" % (x, y))[1]
                record("2. mode %dx%d: %s in its column" % (w, h, name),
                       bg == colour, "0x%06x at %d,%d" % (bg, x, y))

        out = run("meechowm -m mode 1234x567")
        record("3. not a mode of the screen: an error, status 1",
               "error:" in out and "END=1" in out, out.strip())
        out = run("meechowm -m mode 12x")
        record("3. not a mode at all: an error, status 1",
               "error:" in out and "END=1" in out, out.strip())
        out = run("fbmode 800x600")
        record("3. fbmode while the compositor has the screen: refused",
               "a program has the screen" in out and "END=1" in out,
               out.strip())
        st = state()
        record("3. the screen as it was", settled(st, 1280, 720), "%s" % st)

        def modes_now(cmd="meechowm -m modes"):
            out = run(cmd).strip().split("\n")
            return [l.strip() for l in out
                    if re.match(r"\d+x\d+@\d+", l.strip())]

        # Another monitor: the first one out (the line held low long
        # enough to count), then this one in.
        run("$G/fbvplug 0; sleep 1; $G/fbvplug 2")
        st = wait_for(lambda s: settled(s, 1024, 768), 60)
        cols = st["cols"]
        record("6. a 1024x768 monitor under the compositor: taken by it",
               settled(st, 1024, 768) and
               [c["w"] for c in cols] == [half(1024)] * 2,
               "%s" % st)
        got = modes_now()
        record("6. the list is that monitor's",
               got == ["1024x768@60 current", "800x600@60"], "%s" % got)
        geo = fbart("-w 1 -h 1")[0]
        record("6. the frame", geo == (1024, 768, 4096), "%s" % (geo,))
        for c, colour, name in ((0, BLUE, "blue"), (1, RED, "red")):
            if len(cols) < 2:
                break
            x = cols[c]["x"] + cols[c]["w"] // 2
            y = cols[c]["y"] + cols[c]["h"] // 2
            bg = fbart("-x %d -y %d -w 4 -h 4 -s 1" % (x, y))[1]
            record("6. %s in its column" % name, bg == colour,
                   "0x%06x at %d,%d" % (bg, x, y))
        run("$G/fbvplug 0")
        time.sleep(4)
        st = state()
        record("6. taken out: the mode stays", settled(st, 1024, 768),
               "%s" % st)
        run("$G/fbvplug 1")
        st = wait_for(lambda s: settled(s, 1280, 720), 60)
        record("6. the first monitor back: 1280x720 again",
               settled(st, 1280, 720) and
               modes_now() == MODES, "%s" % st)

        run("meechowm -m mode 1024x768")
        wait_for(lambda s: settled(s, 1024, 768), 120)
        run("meechowm -m quit")
        time.sleep(8)
        run("echo; echo the console, after the compositor at 1024x768")
        time.sleep(2)
        geo, _, ink = fbart("-x 0 -y 0 -w 1024 -h 768 -s 4")
        record("4. the console has the screen at 1024x768, with text",
               geo == (1024, 768, 4096) and ink > 200,
               "%s, ink %d" % (geo, ink))

        out = run("fbmode").strip().split("\n")
        got = [l.strip() for l in out if re.match(r"\d+x\d+@\d+", l.strip())]
        record("5. fbmode: the list, 1024x768 current",
               got == ["1280x720@60", "1920x1080@60", "1024x768@60 current",
                       "800x600@60"], "%s" % got)
        for w, h in ((1920, 1080), (800, 600), (1280, 720)):
            out = run("fbmode %dx%d" % (w, h))
            record("5. fbmode %dx%d: taken" % (w, h),
                   "%dx%d@60" % (w, h) in out and "END=0" in out,
                   out.strip())
            run("echo the console at %dx%d" % (w, h))
            time.sleep(2)
            geo, _, ink = fbart("-x 0 -y 0 -w %d -h %d -s 4" % (w, h))
            record("5. fbmode %dx%d: the frame, the console's text on it"
                   % (w, h), geo == (w, h, w * 4) and ink > 200,
                   "%s, ink %d" % (geo, ink))
        out = run("fbmode 640x480")
        record("5. fbmode, a mode the screen does not have: status 1",
               "not a mode of the screen" in out and "END=1" in out,
               out.strip())
        out = run("fbmode 12x")
        record("5. fbmode, not a mode at all: status 2",
               "END=2" in out, out.strip())
        geo = fbart("-w 1 -h 1")[0]
        record("5. the screen as it was", geo == (1280, 720, 5120),
               "%s" % (geo,))

        def console_at(w, h, timeout=30):
            deadline = time.time() + timeout
            while True:
                geo = fbart("-w 1 -h 1")[0]
                if geo == (w, h, w * 4) or time.time() > deadline:
                    break
                time.sleep(1)
            run("echo the console at %dx%d" % (w, h))
            time.sleep(2)
            return fbart("-x 0 -y 0 -w %d -h %d -s 4" % (w, h))

        # Another monitor: the first one out (the line held low long
        # enough to count), then this one in.
        run("$G/fbvplug 0; sleep 1; $G/fbvplug 2")
        geo, _, ink = console_at(1024, 768)
        record("7. a 1024x768 monitor at the console: the driver takes it",
               geo == (1024, 768, 4096) and ink > 200,
               "%s, ink %d" % (geo, ink))
        got = modes_now("fbmode")
        record("7. fbmode lists that monitor's",
               got == ["1024x768@60 current", "800x600@60"], "%s" % got)
        run("$G/fbvplug 0; sleep 1; $G/fbvplug 1")
        geo, _, ink = console_at(1280, 720)
        record("7. the first monitor back: 1280x720, the console on it",
               geo == (1280, 720, 5120) and ink > 200,
               "%s, ink %d" % (geo, ink))
        run("$G/fbvplug 0; sleep 2; $G/fbvplug 1; sleep 3")
        geo = fbart("-w 1 -h 1")[0]
        record("7. the same monitor out and in: the mode stays",
               geo == (1280, 720, 5120), "%s" % (geo,))
        rc = 0 if failed == 0 else 1
        send("/sbin/poweroff")
        time.sleep(5)
    except Failed as e:
        print("FAIL: %s" % e)
        print("--- the compositor's log:")
        try:
            print(run("tail -20 /tmp/mwm.log", 30))
        except Failed:
            pass
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    print("\n%d check(s), %d failed" % (checks, failed))
    sys.exit(rc)


if __name__ == "__main__":
    main()
