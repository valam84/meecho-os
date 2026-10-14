#!/usr/bin/env python3
"""The MEECHO desktop as a whole on QEMU (stage 11.7): meecho-desktop, its
programs and its style, against the compositor's state and the pixels.

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-desk), the
frame buffer driver at virtual=1, and starts the desktop the way a user
does - meecho-desktop from the console, with no font, configuration or
environment of the stand's (fbart is the stand's, to read the screen).

  1. fontconfig: monospace is Go Mono, sans-serif Go (56-meecho.conf)
 1b. terminfo: foot has 256 colours and its pairs (share/terminfo: a
     pairs#65536 the tic of the tree dropped took the colour away from
     every curses program in foot)
  2. meecho-desktop: the wallpaper (wbg, layer "wallpaper", background),
     the panel (yambar) and foot in the first column
  3. the panel: ink (the palette's ground) with text on it, the line
     under it
  4. the wallpaper: right of foot's column, a picture - not one colour,
     not the ground
  5. foot: its background is ink (/etc/xdg/foot/foot.ini)
  6. the panel's load script: cpu and memory as numbers
  7. nnn (as the palette starts it) in a second column, still running
  8. Super+d: the palette (fuzzel), its background the palette's surface
     (/etc/xdg/fuzzel/fuzzel.ini)
  9. grim, through meechowm-shot: a PNG of the screen's size, the same
     pixels as /dev/fb0 at that moment
 9a. in that frame, nnn's directories in MEECHO blue (foot's ANSI blue)
 10. "screenshot" typed into the palette: a PNG in ~/Pictures
 11. Print, alone: another one
 12. meechowm -m quit: the compositor ends, and every program of the
     desktop with it
 13. the console, back: on the palette's ground, not black

The frame of step 9 is written as a PNG (~/meecho-desk.png, -o), and so
is grim's.  At the first failure the tail of the compositor's log is
printed.  Log of the whole run: ~/qemu-desk.log.

    python3 port/gfx/test/qemu-desk.py [-o file.png] [-x]
"""

import argparse
import base64
import gzip
import importlib.util
import os
import re
import select
import signal
import struct
import subprocess
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.join(HOME, "obj-evbarm64/work-desk")
GUESTDIR = "/usr/tests/gfx"
W, H = 1280, 720
PALETTE = os.path.join(HERE, "..", "..", "..", "minix/share/style/palette")

_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)


def palette():
    """The palette's colours, as numbers (what style.awk reads)."""
    col = {}
    for line in open(PALETTE):
        f = line.split("#")[0].split()
        if len(f) == 2:
            col[f[0]] = int(f[1], 16) if re.fullmatch(r"[0-9a-f]{6}", f[1]) \
                else col[f[1]]
    return col


def near(a, b, tol):
    return all(abs((a >> s & 0xff) - (b >> s & 0xff)) <= tol
               for s in (0, 8, 16))


def png_write(path, raw, w, h):
    """XRGB8888 little-endian (B, G, R, X bytes) to an RGB PNG."""
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        line = raw[y * w * 4:(y + 1) * w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3] = line[2::4]
        rgb[1::3] = line[1::4]
        rgb[2::3] = line[0::4]
        rows += rgb

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def png_read(data):
    """A PNG of 8-bit RGB or RGBA (what grim writes) to (w, h, rows of
    RGB bytes)."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise RuntimeError("not a PNG")
    pos, idat = 8, b""
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    if depth != 8 or ctype not in (2, 6):
        raise RuntimeError("PNG depth %d colour type %d" % (depth, ctype))
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    out = []
    for y in range(h):
        ft = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 0xff
            elif ft == 2:
                line[i] = (line[i] + b) & 0xff
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xff
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xff
        prev = line
        rgb = bytearray(w * 3)
        for k in range(3):
            rgb[k::3] = line[k::bpp]
        out.append(bytes(rgb))
    return w, h, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", default=os.path.join(HOME, "meecho-desk.png"))
    ap.add_argument("-x", action="store_true", help="stop at the first "
                    "failure")
    a = ap.parse_args()
    col = palette()

    qemu_meechowm.stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="3000",
               QEMU_SMP="1", DISK_FRESH="1")
    log = open(os.path.join(HOME, "qemu-desk.log"), "w")
    p = subprocess.Popen(["bash", RAMIMAGE, "-d"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         bufsize=0, env=env, start_new_session=True)
    buf = ""
    results = []

    class Stop(Exception):
        pass

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
        p.stdin.write((line + "\n").encode())
        p.stdin.flush()
        time.sleep(0.4)

    runs = [0]

    def run(cmd, timeout=120):
        """cmd, and its output up to a marker of its own: one shared
        marker let an answer be taken for the next command's."""
        runs[0] += 1
        send("%s; echo RU''N%d" % (cmd, runs[0]))
        return expect(r"RUN%d\b" % runs[0], timeout)

    def record(name, ok, detail=""):
        results.append((name, ok))
        print("%s %s%s" % ("ok  " if ok else "FAIL", name,
                           "  (%s)" % detail if detail else ""))
        sys.stdout.flush()
        if not ok:
            send("tail -25 /tmp/desk.log")
            time.sleep(3)
            if a.x:
                raise Stop()

    def state():
        return run("meechowm -m state 2>&1")

    def state_has(pat, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            out = state()
            if re.search(pat, out):
                return out
            time.sleep(3)
        return None

    def fbart(x, y, w, h, step=1):
        out = run("$G/fbart -x %d -y %d -w %d -h %d -s %d | grep FBART" %
                  (x, y, w, h, step))
        m = re.search(r"background 0x([0-9a-f]+).*?FBART ink (\d+)", out,
                      re.S)
        if not m:
            raise RuntimeError("fbart said nothing: %r" % out[-400:])
        return int(m.group(1), 16), int(m.group(2))

    def fetch(cmd, name, timeout=900):
        """A file of the guest (cmd writes it to stdout), through gzip
        and uuencode -m on the console."""
        send("%s | gzip -9 | uuencode -m %s; echo FE''TCHED" % (cmd, name))
        out = expect(r"====\s*\r?\n.*FETCHED", timeout)
        m = re.search(r"begin-base64 \d+ %s\r?\n(.*?)\r?\n====" %
                      re.escape(name), out, re.S)
        if not m:
            raise RuntimeError("nothing came of %s" % name)
        return gzip.decompress(base64.b64decode(
            "".join(l.strip() for l in m.group(1).splitlines())))

    def kbd(label, text):
        run("minix-service up /service/usb_kbd -label %s -args "
            "'inject=%s'" % (label, text))

    rc = 1
    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("G=%s" % GUESTDIR)
        run("minix-service up /service/input -dev /dev/kbdmux; "
            "minix-service up /service/fb -dev /dev/fb0 -args virtual=1",
            60)

        # 1. The typeface.
        out = run("fc-match monospace; fc-match sans-serif", 300)
        record("1. fontconfig: monospace is Go Mono, sans-serif Go",
               re.search(r"Go-Mono\.ttf: \"Go Mono\" \"Regular\"", out) and
               re.search(r"Go-Regular\.ttf: \"Go\" \"Regular\"", out),
               " / ".join(re.findall(r"\S+\.ttf: [^\r\n]*", out)))
        # foot's terminfo: without pairs curses has no colour at all.
        out = run("echo TI $(tput -T foot colors) $(tput -T foot pairs)")
        m = re.search(r"TI (\d+) (\d+)", out)
        record("1b. terminfo: foot has colours and pairs",
               m is not None and m.group(1) == "256" and
               int(m.group(2)) > 0, m.group(0) if m else "?")

        # 2. The desktop, as a user starts it.
        send("meecho-desktop > /tmp/desk.log 2>&1 &")
        send("export XDG_RUNTIME_DIR=/tmp/xdg-0")
        st = state_has(r"layer wallpaper 0 .*\n.*layer panel .*"
                       r"|layer panel .*\n.*layer wallpaper 0 ", 600)
        st2 = state_has(r"column 1\.1 .* title (?!-)", 600)
        record("2. meecho-desktop: wallpaper, panel, foot",
               st is not None and st2 is not None)
        time.sleep(10)

        # 3. The panel.
        bg, ink = fbart(0, 0, W, 23)
        line, _ = fbart(400, 23, 16, 1)
        record("3. the panel: on ink, with text, a line under it",
               bg == col["ink"] and ink > 300 and line == col["line"],
               "background 0x%06x ink %d, line 0x%06x" % (bg, ink, line))

        # 4. The wallpaper, right of foot.
        m = re.search(r"column 1\.1 (-?\d+) (-?\d+) (\d+) (\d+)", st2 or "")
        fx, fw = (int(m.group(1)), int(m.group(3))) if m else (8, 632)
        wx = fx + fw + 40
        bg, ink = fbart(wx, 230, W - wx - 8, 280, 2)
        record("4. the wallpaper: a picture right of foot's column",
               bg != col["ink"] and ink > 500,
               "x %d: background 0x%06x ink %d" % (wx, bg, ink))

        # 5. foot.
        bg, _ = fbart(fx + 20, 600, 200, 40)
        record("5. foot: its background is ink", bg == col["ink"],
               "0x%06x" % bg)

        # 6. The panel's numbers.
        out = run("meechowm-bar load; meechowm-bar status")
        record("6. the panel's scripts: cpu, memory, workspaces",
               re.search(r"cpu\|int\|\d+", out) and
               re.search(r"mem\|int\|[1-9]\d*", out) and
               re.search(r"workspace\|string\|\S", out),
               " ".join(re.findall(r"(?:cpu|mem|workspace)\|\S+\|[^\r\n]*",
                                   out)))

        # 7. The file manager, as the palette starts it.
        send("meechowm -m spawn \"foot -T files -e nnn -e /etc\"")
        st = st2b = state_has(r"column 1\.2 .* title files", 300)
        time.sleep(12)
        out = run("ps -ax -o command | grep -c '^[n]nn'")
        alive = re.search(r"^1\s*$", out, re.M)
        record("7. nnn in a second column, running",
               st is not None and alive is not None)

        # 8. The palette.
        kbd("kbdd", "%[G+d]")
        st = state_has(r"layer launcher", 300)
        time.sleep(8)
        m = re.search(r"layer launcher \d+ (\d+) (\d+) (\d+) (\d+)",
                      st or "")
        if m:
            lx, ly, lw, lh = (int(v) for v in m.groups())
            bg, _ = fbart(lx + lw // 2, ly + lh - 30, 40, 8)
        else:
            bg = 0
        record("8. Super+d: the palette, on the surface colour",
               st is not None and near(bg, col["surface"], 6),
               "background 0x%06x" % bg)
        run("minix-service down kbdd")

        # 9. grim, and the frame it should have seen.
        out = run("meechowm-shot /tmp/shot.png; ls -l /tmp/shot.png", 300)
        made = re.search(r"/tmp/shot\.png\r?\n", out) is not None
        frame = fetch("dd if=/dev/fb0 bs=%d count=%d 2>/dev/null" %
                      (W * 4, H), "frame.gz")
        png_write(a.o, frame, W, H)
        print("     the frame: %s" % a.o)

        # 9a. nnn's directories in MEECHO blue: foot's ANSI blue, which
        # nnn asks for (NNN_FCOLORS) - the list's top, above the palette.
        m = re.search(r"column 1\.2 (-?\d+) (-?\d+) (\d+) (\d+)", st2b or "")
        nx, ny = (int(m.group(1)), int(m.group(2))) if m else (644, 32)
        blue = 0
        for y in range(ny + 50, ny + 190):
            row = frame[y * W * 4:(y + 1) * W * 4]
            for x in range(nx + 30, min(nx + 300, W)):
                px = row[4 * x + 2] << 16 | row[4 * x + 1] << 8 | row[4 * x]
                if near(px, col["accent"], 24):
                    blue += 1
        record("9a. nnn: its directories in MEECHO blue", blue > 150,
               "%d blue pixels" % blue)
        shot = fetch("cat /tmp/shot.png", "shot.gz") if made else b""
        if shot:
            with open(os.path.splitext(a.o)[0] + "-grim.png", "wb") as f:
                f.write(shot)
            gw, gh, rows = png_read(shot)
            diff = 0
            for y in range(min(gh, H)):
                fr = frame[y * W * 4:(y + 1) * W * 4]
                g = rows[y]
                for x in range(0, min(gw, W), 2):
                    if (g[3 * x], g[3 * x + 1], g[3 * x + 2]) != \
                            (fr[4 * x + 2], fr[4 * x + 1], fr[4 * x]):
                        diff += 1
            record("9. grim: a PNG of the screen, the screen's pixels",
                   (gw, gh) == (W, H) and diff < W * H // 2 // 100,
                   "%dx%d, %d of %d pixels differ" % (gw, gh, diff,
                                                      W * H // 2))
        else:
            record("9. grim: a PNG of the screen", False, "no file")

        # 10. "screenshot" in the palette.
        kbd("kbdt", "screenshot%0a")
        time.sleep(15)
        out = run("ls ~/Pictures | grep -c '^screen-.*\\.png$'")
        n1 = re.search(r"^(\d+)\s*$", out, re.M)
        record("10. the palette's Screenshot: a PNG in ~/Pictures",
               n1 is not None and int(n1.group(1)) >= 1,
               n1.group(1) if n1 else "?")
        run("minix-service down kbdt")
        state_has(r"^(?!.*layer launcher)", 60)

        # 11. Print.
        time.sleep(2)
        kbd("kbdp", "%[Print]")
        time.sleep(15)
        out = run("ls ~/Pictures | grep -c '^screen-.*\\.png$'")
        n2 = re.search(r"^(\d+)\s*$", out, re.M)
        record("11. Print: another PNG",
               n1 and n2 and int(n2.group(1)) > int(n1.group(1)),
               n2.group(1) if n2 else "?")
        run("minix-service down kbdp")

        # 12. The end.
        run("meechowm -m quit")
        send("n=0; while [ $n -lt 60 ] && ps -ax -o command | "
             "grep -q -E '^(meechowm|wbg|yambar|foot|nnn)( |$)'; do "
             "sleep 2; n=$((n+1)); done; echo LE''FT; "
             "ps -ax -o command | grep -E '^(meechowm|wbg|yambar|foot|nnn)"
             "( |$)'; echo EN''D")
        out = expect(r"END", 300)
        left = [l for l in out.split("LEFT", 1)[-1].splitlines()
                if re.match(r"(meechowm|wbg|yambar|foot|nnn)( |$)", l)]
        record("12. meechowm -m quit: the desktop goes, all of it",
               not left, ", ".join(left))

        # 13. The console.
        time.sleep(4)
        bg, ink = fbart(0, 0, W, H, 4)
        record("13. the console is back, on ink", bg == col["ink"] and
               ink > 0, "background 0x%06x ink %d" % (bg, ink))

        send("/sbin/poweroff")
        time.sleep(5)
    except Stop:
        pass
    except RuntimeError as e:
        print("FAIL: %s" % e)
        results.append(("run", False))
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    good = sum(1 for _, ok in results if ok)
    print("%d of %d" % (good, len(results)))
    rc = 0 if results and good == len(results) else 1
    sys.exit(rc)


if __name__ == "__main__":
    main()
