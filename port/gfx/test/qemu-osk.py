#!/usr/bin/env python3
"""The desktop's on-screen keyboard on QEMU (stage 11.9): meechowm-osk.

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-osk) with the
frame buffer driver at virtual=1, as qemu-strip.py does (and stages
DESTDIR/usr/tests/gfx with it: fbart and a font), starts meechowm and the
keyboard client (meechowm-osk -v), and types into foot with the mouse -
clicks from usb_mouse inject= on the keys where they are drawn - the way a
person without a keyboard would.  foot runs "cat > /tmp/typed", so what
was typed is the bytes in that file, read back with od: the check does not
share code with the keyboard, the compositor or foot.

Where the keys are is worked out by hand from the layout as PLAN.md and
<minix/libosk.h> describe it - rows of 60 units, five rows (Buffyboard's
look: Ctrl, Tab and the arrows on top, Alt behind ">_"), the keyboard
the bottom 30 per cent of the screen in whole rows: 215 of 720 - not taken
from libosk.  The pointer's position is the stand's own count of the
relative moves it injected, from the top left corner it is first pushed
into.

  1. the client: "meechowm -m state" says the input method is there and
     the keyboard is down (no window wants text)
  2. foot (text-input-v3) opens and has the focus: the keyboard comes up by
     itself - a layer surface "osk" across the bottom, 1280x215 at 505 -
     and the strip gives up its height (area 1280x505)
  3. Shift, H, i, Shift twice: "Hi" typed; Shift locked shows - its key in
     accent, a letter in surface, the ground between keys ink_deep
  4. o, k, Shift, x, space; 123, 4, 2, !; =\\<, {, ~; abc; Ctrl, a; >_,
     Alt, abc, b; Left, Tab, Enter: the line, byte for byte -
     "Hi" "OKx" " " "42!" "{~" ^A ESC b ESC [ D TAB LF
  5. Ctrl, d: end of file at a line's start - cat ends, foot closes, and
     the keyboard goes down by itself; the strip has all of the height back
 5a. touches, from the touch driver without a touch screen (qcom_touch
     inject=@X:Y, fractions of the screen; the digitizer page through the
     mice, meechowm's wl_touch): t, o, u, then two fingers - Shift kept
     down while c is tapped - and Enter: "touC" and a line feed in another
     foot's file.  The first touch makes meechowm's touch device; until the
     client has a wl_touch it comes as the pointer
 5b. Ctrl, d touched: that foot goes, the keyboard goes down
  6. Super+t (usb_kbd inject=) with no window that wants text: the
     keyboard up ("mode on"); "meechowm -m osk" again: down; "osk auto"
  7. the client killed: the input method gone; "meechowm -m osk on" starts
     another (meechowm-osk from the compositor), and it comes up
  8. the palette has the entry (meechowm -m osk)
  9. "meechowm -m quit": the compositor ends with status 0 - a listener
     left on a global of the new protocols would be wlroots' abort()

    python3 port/gfx/test/qemu-osk.py [-k] [-s cores] [-x] [-p] [-w dir]

-x: stop at the first failure; -p: the screen with the keyboard up to a
PNG (~/meecho-osk.png; costs minutes under TCG); -w: another WORK_DIR.
Log: ~/qemu-osk.log (or beside -w).
"""

import argparse
import base64
import gzip
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
SRC = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.join(HOME, "obj-evbarm64/work-osk")
GUESTDIR = "/usr/tests/gfx"
W, H = 1280, 720
KB_H = 215			# 720 * 30 % = 216, down to five whole rows of 43
KB_Y = H - KB_H
ROWS, UNITS = 5, 60

_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)
_spec = importlib.util.spec_from_file_location(
    "qemu_shot", os.path.join(HERE, "qemu-shot.py"))
qemu_shot = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_shot)

# The keys the stand presses: (row, first unit, width in units).  The top
# row of every layer: Ctrl, Tab, the arrows, 10 units each; Enter at the
# bottom right of every layer.
CTRL, TAB = (0, 0, 10), (0, 10, 10)
LEFT = (0, 40, 10)		# up, down, left, right
ENTER = (4, 48, 12)
SHIFT = (3, 0, 9)		# letters
TO_DIGITS = (4, 0, 12)		# "123", letters' bottom left
TO_TERM = (4, 36, 12)		# ">_", beside Enter on the letters
SPACE = (4, 12, 24)		# letters
TO_SYMBOLS = (3, 0, 9)		# "=\<" on the digits
FROM_PUNCT = (4, 0, 9)		# "abc" on the digits and the symbols
FROM_TERM = (4, 0, 12)		# "abc" on the terminal layer
ALT = (1, 10, 10)		# the terminal layer: Esc, Alt, Home, ...
TOP = "qwertyuiop"
MID = "asdfghjkl"
LOW = "zxcvbnm"


def letter(c):
    """Keys 6 wide; the middle row in by half a key, the low row after
    Shift."""
    if c in TOP:
        return (1, 6 * TOP.index(c), 6)
    if c in MID:
        return (2, 3 + 6 * MID.index(c), 6)
    return (3, 9 + 6 * LOW.index(c), 6)


def digit(c):
    return (1, 6 * "1234567890".index(c), 6)


BANG = (3, 33, 6)		# digits, third row: layer " ' : ; ! ? /
LBRACE, TILDE = (1, 54, 6), (1, 6, 6)	# symbols: ` ~ \ | ^ * = < > {

LINE = b"HiOKx 42!{~\x01\x1bb\x1b[D\t\n"


def centre(key):
    r, u, w = key
    return ((2 * u + w) * W // (2 * UNITS),
            KB_Y + (2 * r + 1) * KB_H // (2 * ROWS))


def style():
    """name -> 0xRRGGBB, from MEECHO's palette, as style.awk has it."""
    out = subprocess.run(["awk", "-v", "out=c", "-f",
                          os.path.join(SRC, "minix/share/style/style.awk"),
                          os.path.join(SRC, "minix/share/style/palette")],
                         capture_output=True, text=True, check=True).stdout
    return {m.group(1): int(m.group(2), 16) for m in
            re.finditer(r"#define STYLE_(\w+)\s+0x([0-9a-fA-F]{6})", out)}


STATE_TOP = re.compile(r"screen (\d+)x(\d+) area (\d+),(\d+) (\d+)x(\d+) ")
STATE_LAYER = re.compile(r"layer osk (\d+) (-?\d+) (-?\d+) (\d+) (\d+) "
                         r"keyboard (\S+) (\S+)")
STATE_OSK = re.compile(r"osk (up|down) mode (\S+) method (\d) fields (\d+) "
                       r"active (\d+)")
STATE_COL = re.compile(r"column \d+\.\d+ (-?\d+) (-?\d+) (\d+) (\d+) width "
                       r"\S+ (\S+) at -?\d+ size \d+x\d+ pid (\d+) title")


def parse_state(text):
    st = {"cols": []}
    m = STATE_TOP.search(text)
    if m:
        st["area"] = tuple(int(m.group(i)) for i in range(3, 7))
    m = STATE_LAYER.search(text)
    if m:
        st["layer"] = tuple(int(m.group(i)) for i in range(1, 6)) + \
            (m.group(6),)
    m = STATE_OSK.search(text)
    if m:
        st["osk"], st["mode"], st["method"] = m.group(1), m.group(2), \
            int(m.group(3))
        st["fields"], st["active"] = int(m.group(4)), int(m.group(5))
    for m in STATE_COL.finditer(text):
        st["cols"].append({"y": int(m.group(2)), "h": int(m.group(4)),
                           "focus": m.group(5) == "focus",
                           "pid": int(m.group(6))})
    return st


def main():
    global WORK
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-x", action="store_true",
                    help="stop at the first failure")
    ap.add_argument("-p", action="store_true", help="a PNG of the screen")
    ap.add_argument("-w", help="WORK_DIR (default %s)" % WORK)
    a = ap.parse_args()
    logpath = os.path.join(HOME, "qemu-osk.log")
    if a.w:
        WORK = os.path.abspath(a.w)
        logpath = WORK + ".log"
    col = style()

    if not a.k:
        qemu_meechowm.stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="3000",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(logpath, "w")
    p = subprocess.Popen(["bash", RAMIMAGE, "-d"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         bufsize=0, env=env, start_new_session=True)
    buf = ""
    results = []
    dumped = [False]

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
                raise RuntimeError("no %r" % pat)
            if select.select([p.stdout], [], [], 0.2)[0]:
                c = p.stdout.read(4096 if "begin-base64" in buf else 1)
                c = c.decode(errors="replace")
                buf += c
                log.write(c)
                log.flush()

    def send(line):
        assert len(line) < 250, "the tty cuts lines at about 255"
        p.stdin.write((line + "\n").encode())
        p.stdin.flush()
        time.sleep(0.4)

    def record(name, ok, why=""):
        results.append(ok)
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()
        if not ok and not dumped[0]:
            dumped[0] = True
            send("tail -25 /tmp/mwm.log; tail -25 /tmp/osk.log; "
                 "echo LO''GGED")
            out = expect(r"LOGGED\r?\n", 60)
            for l in out.splitlines()[1:-1]:
                print("      | " + l)
        if not ok and a.x:
            raise RuntimeError("stopped at the first failure")

    def state():
        send("meechowm -m state; echo ST''DONE")
        return parse_state(expect(r"STDONE\r?\n", 120))

    def wait_for(pred, timeout=150):
        deadline = time.time() + timeout
        while True:
            st = state()
            if pred(st) or time.time() > deadline:
                return st
            time.sleep(2)

    def colour_at(x, y, size=8):
        send("$G/fbart -x %d -y %d -w %d -h %d -s 2; echo FB''DONE" %
             (x - size // 2, y - size // 2, size, size))
        out = expect(r"FBDONE\r?\n", 120)
        m = re.search(r"background 0x([0-9a-f]{6})\b", out)
        return int(m.group(1), 16) if m else -1

    # The pointer: where the stand has put it.  The first move pushes it
    # into the top left corner, wherever it was.
    ptr = [None]
    mice = [0]

    def click(keys):
        reports = []
        if ptr[0] is None:
            reports += ["0:-127:-127"] * 12
            ptr[0] = (0, 0)
        for k in keys:
            tx, ty = centre(k)
            x, y = ptr[0]
            while (x, y) != (tx, ty):
                dx = max(-127, min(127, tx - x))
                dy = max(-127, min(127, ty - y))
                reports.append("0:%d:%d" % (dx, dy))
                x, y = x + dx, y + dy
            reports += ["1:0:0", "0:0:0"]
            ptr[0] = (x, y)
        assert len(reports) <= 64, "usb_mouse takes 64 reports"
        mice[0] += 1
        send("minix-service up /service/usb_mouse -label mousek%d -args "
             "'inject=%s'" % (mice[0], ",".join(reports)))
        send("sleep %d; minix-service down mousek%d; echo CL''ICK" %
             (3 + len(reports) // 50, mice[0]))
        expect(r"CLICK\r?\n", 120)

    def clicks(keys):
        """As many injections as 64 reports a time takes."""
        i = 0
        while i < len(keys):
            n = len(keys) - i
            while True:
                try:
                    saved = ptr[0]
                    click(keys[i:i + n])
                    break
                except AssertionError:
                    ptr[0] = saved
                    n -= 1
            i += n

    rc = 1
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
        # Its exit status to a file (step 9): an interactive ash frees a
        # finished job at its next prompt, and wait answers 127 after that.
        send("rm -f /tmp/mwm.rc; (MEECHOWM_ANIM_MS=0 MEECHOWM_DEBUG=1 "
             "meechowm > /tmp/mwm.log 2>&1; echo $? > /tmp/mwm.rc) &")
        send("n=0; while [ $n -lt 90 ] && ! meechowm -m state >/dev/null "
             "2>&1; do sleep 2; n=$((n+1)); done; echo WA''ITED $n")
        expect(r"WAITED \d+", 300)
        send("export WAYLAND_DISPLAY=wayland-0")
        send("meechowm-osk -v > /tmp/osk.log 2>&1 & OSK=$!")
        send("sleep 10; cat /tmp/osk.log; echo OS''KLOG")
        for l in expect(r"OSKLOG\r?\n", 120).splitlines()[1:-1]:
            print("      osk | " + l)

        # 1. The client, and nothing wants text.
        st = wait_for(lambda s: s.get("method") == 1, 300)
        record("1. the input method is there, the keyboard down",
               st.get("method") == 1 and st.get("osk") == "down" and
               "layer" not in st, "state: %s" % st)

        # 2. foot, which has a text field: the keyboard comes up.
        send("rm -f /tmp/typed; foot -o main.workers=0 -f 'DejaVu Sans "
             "Mono:size=14' sh -c 'cat > /tmp/typed' > /tmp/foot.log 2>&1 &")
        st = wait_for(lambda s: s.get("osk") == "up" and "layer" in s and
                      s["cols"] and s["layer"][4] == KB_H, 400)
        lay = st.get("layer")
        record("2. foot has the focus: the keyboard up by itself",
               st.get("osk") == "up" and st.get("active", 0) > 0 and
               lay is not None and lay[0] == 2 and lay[1:5] ==
               (0, KB_Y, W, KB_H) and lay[5] == "none" and
               st.get("area") == (0, 0, W, KB_Y) and
               all(c["y"] + c["h"] <= KB_Y for c in st["cols"]),
               "state: %s" % st)
        time.sleep(3)

        # 3. Shift H i, Shift Shift: locked.
        clicks([SHIFT, letter("h"), letter("i"), SHIFT, SHIFT])
        time.sleep(2)
        sx, sy = centre(SHIFT)
        lx, ly = centre(letter("q"))
        shift, key = colour_at(sx - 50, sy - 15, 6), colour_at(lx, ly - 15, 6)
        ground = colour_at(W // 2, KB_Y + 1, 2)
        record("3. Shift locked shows", shift == col["ACCENT"] and
               key == col["SURFACE"] and ground == col["INK_DEEP"],
               "Shift 0x%06x, q 0x%06x, between rows 0x%06x" %
               (shift, key, ground))
        if a.p:
            send("dd if=/dev/fb0 bs=5120 count=%d 2>/dev/null | gzip -9 | "
                 "uuencode -m shot.gz; echo SH''OT" % H)
            out = expect(r"====\s*\r?\n.*SHOT", 900)
            m = re.search(r"begin-base64 \d+ shot\.gz\r?\n(.*?)\r?\n====",
                          out, re.S)
            raw = gzip.decompress(base64.b64decode(
                "".join(l.strip() for l in m.group(1).splitlines())))
            png = os.path.join(HOME, "meecho-osk.png")
            qemu_shot.png(png, raw, W, H)
            print("      the screen: %s" % png)

        # 4. The rest of the line, through every layer and modifier.
        # Alt is on the terminal layer: armed there, it stays armed back on
        # the letters, for b.
        clicks([letter("o"), letter("k"), SHIFT, letter("x"), SPACE,
                TO_DIGITS, digit("4"), digit("2"), BANG,
                TO_SYMBOLS, LBRACE, TILDE, FROM_PUNCT,
                CTRL, letter("a"), TO_TERM, ALT, FROM_TERM, letter("b"),
                LEFT, TAB, ENTER])
        time.sleep(3)
        send("od -An -tx1 /tmp/typed | tr -d ' \\n'; echo; echo OD''DONE")
        out = expect(r"ODDONE\r?\n", 60)
        m = re.search(r"\n([0-9a-f]*)\r?\n", out)
        got = bytes.fromhex(m.group(1)) if m else b"?"
        record("4. the line, byte for byte", got == LINE,
               "typed %r, wanted %r" % (got, LINE))

        # 5. Ctrl d: cat ends, foot goes, the keyboard goes down.
        clicks([CTRL, letter("d")])
        st = wait_for(lambda s: not s["cols"] and s.get("osk") == "down",
                      200)
        record("5. foot gone, the keyboard down by itself",
               not st["cols"] and st.get("osk") == "down" and
               "layer" not in st and st.get("area") == (0, 0, W, H),
               "state: %s" % st)

        # 5a. Touches: the touch driver without a touch screen
        # (qcom_touch inject=, fractions of the screen) - a contact on a
        # key is a key, two fingers are two: Shift held, c tapped.
        send("rm -f /tmp/touched; foot -o main.workers=0 -f 'DejaVu Sans "
             "Mono:size=14' sh -c 'cat > /tmp/touched' > /tmp/foot.log "
             "2>&1 &")
        st = wait_for(lambda s: s.get("osk") == "up" and "layer" in s and
                      s["cols"], 400)
        time.sleep(3)

        def frac(key):
            x, y = centre(key)
            return "%d:%d" % (x * 65535 // (W - 1), y * 65535 // (H - 1))

        def touch(items):
            send("minix-service up /service/qcom_touch -label toucho -args "
                 "'inject=%s'" % ",".join(items))
            send("sleep %d; minix-service down toucho; echo TO''UCH" %
                 (3 + len(items) // 25))
            expect(r"TOUCH\r?\n", 120)

        touch(["~25"] + ["@" + frac(letter(c)) for c in "tou"] +
              ["+@" + frac(SHIFT), "~5", "@" + frac(letter("c")), "~5",
               "-@", "@" + frac(ENTER)])
        time.sleep(3)
        send("od -An -tx1 /tmp/touched | tr -d ' \\n'; echo; echo OD''DONE")
        out = expect(r"ODDONE\r?\n", 60)
        m = re.search(r"\n([0-9a-f]*)\r?\n", out)
        got = bytes.fromhex(m.group(1)) if m else b"?"
        send("grep -c 'touch screen: in the mice' /tmp/mwm.log; echo TS''EEN")
        seen = re.search(r"\n(\d+)\r?\n", expect(r"TSEEN\r?\n", 60))
        record("5a. touched: keys, two fingers at once", got == b"touC\n" and
               seen is not None and seen.group(1) != "0",
               "typed %r; meechowm's touch device: %s" %
               (got, seen.group(1) if seen else "?"))
        touch(["~25", "@" + frac(CTRL), "@" + frac(letter("d"))])
        st = wait_for(lambda s: not s["cols"] and s.get("osk") == "down",
                      200)
        record("5b. Ctrl d touched: foot gone, the keyboard down",
               not st["cols"] and st.get("osk") == "down",
               "state: %s" % st)

        # 6. Super+t; the command; auto.
        send("minix-service up /service/usb_kbd -label kbdk -args "
             "'inject=%[G+t]'")
        send("sleep 3; minix-service down kbdk; echo KE''YS")
        expect(r"KEYS\r?\n", 120)
        st = wait_for(lambda s: s.get("osk") == "up" and "layer" in s, 120)
        up = st.get("osk") == "up" and st.get("mode") == "on" and \
            "layer" in st
        send("meechowm -m osk")
        st2 = wait_for(lambda s: "layer" not in s, 60)
        send("meechowm -m osk auto")
        st3 = state()
        record("6. Super+t up, osk down, osk auto", up and
               st2.get("osk") == "down" and st2.get("mode") == "off" and
               "layer" not in st2 and st3.get("mode") == "auto" and
               st3.get("osk") == "down",
               "after Super+t %s; after osk %s; after auto %s" %
               (st, st2, st3))

        # 7. No client: asking for the keyboard starts one.
        send("kill $OSK")
        st = wait_for(lambda s: s.get("method") == 0, 60)
        gone = st.get("method") == 0
        send("meechowm -m osk on")
        st = wait_for(lambda s: s.get("method") == 1 and "layer" in s, 300)
        record("7. a client started by the compositor comes up",
               gone and st.get("method") == 1 and st.get("osk") == "up" and
               "layer" in st, "killed: method %s; then %s" %
               ("0" if gone else "1", st))

        # 8. The palette.
        send("grep -c 'meechowm -m osk' /usr/bin/meechowm-palette; "
             "echo PA''L")
        out = expect(r"PAL\r?\n", 60)
        record("8. the palette has it", re.search(r"\n1\r?\n", out)
               is not None, out.strip())

        # 9. The end.
        send("meechowm -m quit; n=0; while [ ! -f /tmp/mwm.rc ] && "
             "[ $n -lt 30 ]; do sleep 1; n=$((n+1)); done; "
             "echo R''C=$(cat /tmp/mwm.rc 2>/dev/null || echo none)")
        out = expect(r"RC=(\S+)\r?\n", 120)
        rcv = re.search(r"RC=(\S+)", out).group(1)
        record("9. the compositor ends with status 0", rcv == "0",
               "status %s" % rcv)

        send("/sbin/poweroff")
        time.sleep(8)
        rc = 0
    except RuntimeError as e:
        print("FAIL: %s" % e)
        results.append(False)
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
        log.close()
    bad = results.count(False)
    print("%d of %d checks failed" % (bad, len(results)))
    sys.exit(1 if bad or rc else 0)


if __name__ == "__main__":
    main()
