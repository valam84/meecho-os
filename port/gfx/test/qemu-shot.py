#!/usr/bin/env python3
"""A picture of the desktop on QEMU: the frame on display, as a PNG.

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-shot) with
the frame buffer driver at virtual=1 - there is no display controller, so
QEMU's own screendump has nothing to show - stages what qemu-meechowm.py
stages (fbart, a font), and sets a desktop up: meechowm with its panel
(meechowm-bar: yambar), foot in the first column, a window of one colour
in the second, and with -p the palette (Super+d: fuzzel) open over them.
Then the guest reads the frame on display through /dev/fb0, gzip and
uuencode -m carry it over the console, and the host writes it as a PNG
(XRGB8888, no library needed).

    python3 port/gfx/test/qemu-shot.py [-p] [-o file.png]

Default output: ~/meecho-shot.png.  Log: ~/qemu-shot.log.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-shot")
GUESTDIR = "/usr/tests/gfx"
W, H = 1280, 720

_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)


def png(path, raw, w, h):
    """XRGB8888 little-endian (B, G, R, X bytes) to an RGB PNG."""
    rows = bytearray()
    for y in range(h):
        rows.append(0)		# no filter
        line = raw[y * w * 4:(y + 1) * w * 4]
        for x in range(w):
            b, g, r = line[4 * x], line[4 * x + 1], line[4 * x + 2]
            rows += bytes((r, g, b))

    def chunk(kind, data):
        c = struct.pack(">I", len(data)) + kind + data
        return c + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", action="store_true", help="the palette open")
    ap.add_argument("-o", default=os.path.join(HOME, "meecho-shot.png"))
    a = ap.parse_args()

    qemu_meechowm.stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="2400",
               QEMU_SMP="1", DISK_FRESH="1")
    log = open(os.path.join(HOME, "qemu-shot.log"), "w")
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

    def state_has(pat, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            send("meechowm -m state; echo ST''DONE")
            out = expect(r"STDONE", 120)
            if re.search(pat, out):
                return True
            time.sleep(3)
        return False

    rc = 1
    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("G=%s; mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg"
             % GUESTDIR)
        send("export FONTCONFIG_FILE=$G/fonts.conf")
        send("export MEECHOWM_TERM=\"foot -o main.workers=0 "
             "-f 'DejaVu Sans Mono:size=12'\"")
        send("minix-service up /service/input -dev /dev/kbdmux; "
             "minix-service up /service/fb -dev /dev/fb0 -args virtual=1; "
             "echo UP''DONE")
        expect(r"UPDONE", 60)
        send("meechowm > /tmp/mwm.log 2>&1 &")
        send("n=0; while [ $n -lt 90 ] && ! meechowm -m state >/dev/null "
             "2>&1; do sleep 2; n=$((n+1)); done; echo WA''ITED")
        expect(r"WAITED", 300)
        send("meechowm -m spawn meechowm-bar")
        state_has(r"layer panel", 300)
        send("meechowm -m spawn foot -o main.workers=0 "
             "-f \"'DejaVu Sans Mono:size=12'\" -e /bin/sh -c "
             "\"'uname -a; echo; ls /; echo; exec /bin/sh -i'\"")
        state_has(r"column 1\.1 .* title (?!-)", 300)
        send("meechowm -m spawn $G/wlhello -f -t 3600 -c 0xff3a7bd5 "
             "-T blue")
        state_has(r"column 1\.2 ", 200)
        if a.p:
            send("minix-service up /service/usb_kbd -label kbdshot -args "
                 "'inject=%[G+d]'")
            state_has(r"layer launcher", 300)
            send("minix-service down kbdshot")
        time.sleep(8)
        send("dd if=/dev/fb0 bs=5120 count=%d 2>/dev/null | gzip -9 | "
             "uuencode -m shot.gz; echo SH''OT" % H)
        out = expect(r"====\s*\r?\n.*SHOT", 600)
        m = re.search(r"begin-base64 \d+ shot\.gz\r?\n(.*?)\r?\n====", out,
                      re.S)
        if not m:
            raise RuntimeError("no picture in the output")
        raw = gzip.decompress(base64.b64decode(
            "".join(l.strip() for l in m.group(1).splitlines())))
        if len(raw) < W * H * 4:
            raise RuntimeError("%d bytes, not %d" % (len(raw), W * H * 4))
        png(a.o, raw, W, H)
        print("written: %s" % a.o)
        rc = 0
        send("/sbin/poweroff")
        time.sleep(5)
    except RuntimeError as e:
        print("FAIL: %s" % e)
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    sys.exit(rc)


if __name__ == "__main__":
    main()
