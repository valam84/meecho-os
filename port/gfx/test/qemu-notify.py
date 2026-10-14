#!/usr/bin/env python3
"""A restart is seen (stage 11.7, "Стойкость"): the card meechowm-notify puts
on the desktop when RS restarts a service, on QEMU.

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-notify[-M])
with the frame buffer at virtual=1, as qemu-xray.py does; meechowm, and in
it meechowm-notify as meecho-desktop starts it, plus its -d twin on the
console for the record.  Then, each step against something that does not
share its code:

  cost      what a look at /proc/service costs here (-m: milliseconds and
            messages a look; informational - TCG's milliseconds are not
            the board's)
  quiet     a while after it started, no card: what RS did before the
            notifier came up is not news ("meechowm -m state" has no
            meechowm-notify layer)
  card      "minix-service refresh lwip": a meechowm-notify layer is up,
            at the top right corner (state), it takes no keys, and the -d
            twin says "card restart lwip restarted" with lwip's pid and
            endpoint as /proc/service has them now
  expiry    the card goes by itself (-t)
  picture   a second refresh, and the card on the screen (the rows of
            /dev/fb0 where state says the layer is) is, pixel for pixel in
            its first line, the card "meechowm-notify -R restart lwip"
            draws from the stand's word, not from any event; and is not the
            one it draws for "uds" - so that the comparison can tell names
            apart.  The stripe is the palette's yellow.  The rows go to a
            PNG (-o)

-M NAME runs the same against a mutated copy - one plausible way to lose
the event - built here into DESTDIR/usr/tests/gfx and removed after; the
stand must fail:

  lost      svc.c does not see a new endpoint or a grown count of restarts
  unshown   notify.c learns of the event and never puts the card up

    python3 port/gfx/test/qemu-notify.py [-M lost|unshown] [-s cores]
                                         [-o file.png] [-x]

-x: stop at the first failure.  Log: ~/qemu-notify[-M].log.
"""

import argparse
import base64
import gzip
import importlib.util
import os
import re
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
SRC = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
OBJ = os.path.join(HOME, "obj-evbarm64/minix/usr.bin/meechowm-notify")
NOTIFY_SRC = os.path.join(SRC, "minix/usr.bin/meechowm-notify")
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/gfx"
W, H = 1280, 720

# name: (file, what, what instead) - a plausible way to lose the event.
MUTATIONS = {
    "lost": ("svc.c",
             "if (s->endpoint != ep || s->restarts != restarts) {",
             "if (0) {"),
    "unshown": ("notify.c", "\t\tif (changed)\n\t\t\tshow();",
                "\t\tif (0)\n\t\t\tshow();"),
}

_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)
_spec = importlib.util.spec_from_file_location(
    "qemu_shot", os.path.join(HERE, "qemu-shot.py"))
qemu_shot = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_shot)


def style():
    """STYLE_NAME -> 0xRRGGBB, from MEECHO's palette, as style.awk has it."""
    out = subprocess.run(["awk", "-v", "out=c", "-f",
                          os.path.join(SRC, "minix/share/style/style.awk"),
                          os.path.join(SRC, "minix/share/style/palette")],
                         capture_output=True, text=True, check=True).stdout
    return {m.group(1): int(m.group(2), 16) for m in
            re.finditer(r"#define STYLE_(\w+)\s+0x([0-9a-fA-F]{6})", out)}


def build_mutant(name, out):
    """The notifier with one mutation, linked as its Makefile links it,
    from the generated files of its obj directory."""
    f, old, new = MUTATIONS[name]
    tmp = tempfile.mkdtemp(prefix="notify-mut-")
    for s in ("notify.c", "svc.c", "card.c", "notify.h"):
        shutil.copy2(os.path.join(NOTIFY_SRC, s), tmp)
    path = os.path.join(tmp, f)
    text = open(path).read()
    if text.count(old) != 1:
        sys.exit("mutation %s: %r is not once in %s" % (name, old, f))
    open(path, "w").write(text.replace(old, new))
    for g in ("style.h", "wlr-layer-shell-unstable-v1-client-protocol.h",
              "wlr-layer-shell-protocol.c", "xdg-shell-protocol.c"):
        if not os.path.exists(os.path.join(OBJ, g)):
            sys.exit("no %s in %s: port/build-dirs.sh "
                     "minix/usr.bin/meechowm-notify first" % (g, OBJ))
    inc = os.path.join(DEST, "usr/include")
    cmd = [XCC, "-O2", "-static", "-std=gnu99", "--sysroot=" + DEST,
           "-D_NETBSD_SOURCE", "-I" + tmp, "-I" + OBJ,
           "-I" + inc + "/pixman-1", "-I" + inc + "/freetype2",
           "-I" + inc + "/gfxcompat", "-o", out] + \
        [os.path.join(tmp, s) for s in ("notify.c", "svc.c", "card.c")] + \
        [os.path.join(OBJ, s) for s in ("wlr-layer-shell-protocol.c",
                                        "xdg-shell-protocol.c")] + \
        ["-lfcft", "-lfontconfig", "-lfreetype", "-lexpat", "-lz",
         "-lpixman-1", "-lwayland-client", "-lepoll-shim", "-lffi",
         "-lgfxcompat", "-lmthread", "-lm"]
    if subprocess.call(cmd) != 0:
        sys.exit("build failed: the %s mutant" % name)
    shutil.rmtree(tmp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-M", choices=sorted(MUTATIONS))
    ap.add_argument("-s", type=int, default=1)
    ap.add_argument("-o", default=os.path.join(HOME, "meecho-notify.png"))
    ap.add_argument("-x", action="store_true")
    a = ap.parse_args()
    st = style()
    tag = "-" + a.M if a.M else ""
    work = os.path.join(HOME, "obj-evbarm64/work-notify" + tag)

    if not os.path.exists(os.path.join(DEST, "usr/bin/meechowm-notify")):
        sys.exit("not in DESTDIR: /usr/bin/meechowm-notify "
                 "(port/build-dirs.sh minix/usr.bin/meechowm-notify)")
    qemu_meechowm.stage()
    mutant = None
    binary = "/usr/bin/meechowm-notify"
    if a.M:
        mutant = os.path.join(DEST, GUESTDIR.lstrip("/"),
                              "meechowm-notify-" + a.M)
        build_mutant(a.M, mutant)
        binary = "%s/meechowm-notify-%s" % (GUESTDIR, a.M)
        print("mutant %s: %s" % (a.M, binary))
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ, WORK_DIR=work, QEMU_TIMEOUT="3600",
               QEMU_SMP=str(a.s), DISK_FRESH="1")
    log = open(os.path.join(HOME, "qemu-notify%s.log" % tag), "w")
    p = subprocess.Popen(["bash", RAMIMAGE, "-d"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         bufsize=0, env=env, start_new_session=True)
    buf = ""
    results = []
    imaged = [False]

    def expect(pat, timeout):
        nonlocal buf
        deadline = time.time() + timeout
        while True:
            m = re.search(pat, buf, re.S)
            if m:
                out = buf[:m.end()]
                buf = buf[m.end():]
                return out, m
            if time.time() > deadline or p.poll() is not None:
                raise RuntimeError("no %r" % pat)
            if select.select([p.stdout], [], [], 0.2)[0]:
                c = p.stdout.read(4096 if "begin-base64" in buf else 1)
                c = c.decode(errors="replace")
                buf += c
                log.write(c)
                log.flush()
                # The mutant is in the disk image by the login prompt; out of
                # DESTDIR then, where it would go into everybody's next one.
                if mutant and not imaged[0] and "login:" in buf:
                    imaged[0] = True
                    os.remove(mutant)

    def send(line):
        assert len(line) < 250, "the tty cuts lines at about 255"
        p.stdin.write((line + "\n").encode())
        p.stdin.flush()
        time.sleep(0.4)

    def put_file(path, text):
        """A file in the guest, a short line of printf at a time."""
        send(": > %s" % path)
        for line in text.splitlines():
            while True:
                part, line = line[:120], line[120:]
                q = part.replace("\\", "\\\\").replace("%", "%%") \
                    .replace("'", "'\\''")
                send("printf '%s' >> %s" % (q, path))
                if not line:
                    break
            send("echo >> %s" % path)

    def check(name, ok, detail):
        results.append(ok)
        print("  %s %-8s %s" % ("ok  " if ok else "FAIL", name, detail))
        sys.stdout.flush()
        if not ok and a.x:
            raise RuntimeError("stopped at the first failure")

    def state():
        send("meechowm -m state; echo ST''DONE")
        out, _ = expect(r"STDONE\r?\n", 120)
        return out

    LAYER = (r"^layer meechowm-notify (\d+) (-?\d+) (-?\d+) (\d+) (\d+) "
             r"keyboard (\S+)")

    def layer_within(timeout, up=True):
        deadline = time.time() + timeout
        while True:
            out = state()
            m = re.search(LAYER, out, re.M)
            if (m is not None) == up or time.time() > deadline:
                return m
            time.sleep(2)

    def fetch(cmd, name, timeout):
        """What cmd writes to stdout, gzipped and uuencoded across."""
        send("%s | gzip -9 | uuencode -m %s; echo FE''TCH" % (cmd, name))
        out, _ = expect(r"====\s*\r?\n.*FETCH", timeout)
        m = re.search(r"begin-base64 \d+ %s\r?\n(.*?)\r?\n====" %
                      re.escape(name), out, re.S)
        if not m:
            raise RuntimeError("no %s in the output" % name)
        return gzip.decompress(base64.b64decode(
            "".join(l.strip() for l in m.group(1).splitlines())))

    def rgb(raw, w, x, y):
        o = (y * w + x) * 4
        return raw[o + 2] << 16 | raw[o + 1] << 8 | raw[o]

    def reference(kind, label):
        send("%s -R -H %d -o /tmp/ref-%s.raw %s %s; echo RE''F" %
             (binary, H, label, kind, label))
        out, m = expect(r"card (\d+) (\d+) band (\d+) (\d+) (\d+) (\d+)"
                        r"\r?\n.*?REF\r?\n", 120)
        g = [int(m.group(i)) for i in range(1, 7)]
        raw = fetch("cat /tmp/ref-%s.raw" % label, "ref-%s.gz" % label, 300)
        if len(raw) != g[0] * g[1] * 4:
            raise RuntimeError("reference %s: %d bytes" % (label, len(raw)))
        return g, raw

    def refresh(n):
        send("minix-service refresh lwip; echo REFRESHED %d $?" % n)
        _, m = expect(r"REFRESHED %d (\d+)\r?\n" % n, 300)
        return int(m.group(1))

    rc = 1
    try:
        expect(r"login:", 600)
        send("root")
        expect(r"# ", 60)
        send("G=%s; mkdir -p -m 700 /tmp/xdg; export XDG_RUNTIME_DIR=/tmp/xdg"
             % GUESTDIR)
        send("export FONTCONFIG_FILE=$G/fonts.conf WAYLAND_DISPLAY=wayland-0")
        send("minix-service up /service/input -dev /dev/kbdmux; "
             "minix-service up /service/fb -dev /dev/fb0 -args virtual=1; "
             "echo UP''DONE")
        expect(r"UPDONE", 60)
        send("MEECHOWM_ANIM_MS=0 meechowm > /tmp/mwm.log 2>&1 &")
        send("n=0; while [ $n -lt 90 ] && ! meechowm -m state >/dev/null "
             "2>&1; do sleep 2; n=$((n+1)); done; echo WA''ITED")
        expect(r"WAITED", 300)

        # cost
        send("%s -m 20; echo CO''ST" % binary)
        out, _ = expect(r"COST\r?\n", 300)
        m = re.search(r"look (\d+) services (\d+) ms ([\d.]+) messages "
                      r"([\d.]+)", out)
        print("      a look: %s" % (m.group(0) if m else "no answer"))

        # quiet
        send("%s -t 30000 > /tmp/nt.log 2>&1 & echo NT $!" % binary)
        _, m = expect(r"NT (\d+)\r?\n", 60)
        notifier = int(m.group(1))
        send("%s -d > /tmp/ntd.log 2>&1 &" % binary)
        send("n=0; while [ $n -lt 60 ] && ! grep -q services /tmp/ntd.log; "
             "do sleep 1; n=$((n+1)); done; cat /tmp/ntd.log; echo DU''P")
        out, _ = expect(r"DUP\r?\n", 120)
        time.sleep(8)
        m = layer_within(0, up=True)
        check("quiet", m is None, "no card before anything restarted (%s)" %
              (re.search(r"services \d+", out).group(0)
               if re.search(r"services \d+", out) else "-d said nothing"))

        # card
        r = refresh(1)
        m = layer_within(120)
        ok = m is not None and m.group(6) == "none"
        where = ""
        if m:
            x, y, w, h = (int(m.group(i)) for i in (2, 3, 4, 5))
            ok = ok and x + w == W - 8 and y >= 0 and y < H // 4
            where = "%dx%d at (%d,%d), keyboard %s" % (w, h, x, y,
                                                      m.group(6))
        send("sleep 2; grep '^card' /tmp/ntd.log; cat /proc/service/lwip; "
             "echo CA''RD")
        out, _ = expect(r"CARD\r?\n", 120)
        said = re.search(r"^card restart lwip restarted \| pid (\d+) \S+ "
                         r"endpoint (-?\d+) ", out, re.M)
        pid = re.search(r"^pid:\s+(\d+)", out, re.M)
        ep = re.search(r"^endpoint:\s+(-?\d+)", out, re.M)
        facts = said is not None and pid is not None and ep is not None and \
            said.group(1) == pid.group(1) and said.group(2) == ep.group(1)
        check("card", r == 0 and ok and facts,
              "refresh %d; layer %s; -d %s" % (r, where or "not up",
              said.group(0) if said else "said nothing about lwip"))

        # expiry
        m = layer_within(120, up=False) if m else None
        check("expiry", m is None and ok, "the card went by itself")

        # picture
        send("kill %d" % notifier)
        send("%s -t 600000 > /tmp/nt2.log 2>&1 &" % binary)
        time.sleep(15)
        r = refresh(2)
        m = layer_within(120)
        if m is None:
            check("picture", False, "no card after the second refresh")
        else:
            x, y, w, h = (int(m.group(i)) for i in (2, 3, 4, 5))
            (cw, ch, bx, by, bw, bh), ref = reference("restart", "lwip")
            _, other = reference("restart", "uds")
            time.sleep(2)
            screen = fetch("dd if=/dev/fb0 bs=%d skip=%d count=%d "
                           "2>/dev/null" % (W * 4, y, h), "rows.gz", 600)
            if len(screen) < W * h * 4:
                raise RuntimeError("%d bytes of the screen, not %d" %
                                   (len(screen), W * h * 4))
            qemu_shot.png(a.o, screen, W, h)
            print("      the card's rows: %s" % a.o)
            # The one card is the whole layer.  Not x + w - cw: the
            # reference says "pid 0 · endpoint 0", and its width - the
            # second line's - is not the card's on the screen.
            cx = x
            bad = diff = 0
            for yy in range(by, by + bh):
                for xx in range(bx, bx + bw):
                    s = rgb(screen, W, cx + xx, yy)
                    bad += s != rgb(ref, cw, xx, yy)
                    diff += s != rgb(other, cw, xx, yy)
            stripe = rgb(screen, W, cx + 1, ch // 2)
            check("picture", r == 0 and bad == 0 and diff > 0 and
                  stripe == st["YELLOW"],
                  "first line %dx%d: %d pixels off the card for lwip, %d off "
                  "the one for uds; stripe 0x%06x" % (bw, bh, bad, diff,
                                                     stripe))

        send("meechowm -m quit; sleep 3; /sbin/poweroff")
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
        subprocess.call(["pkill", "-9", "-f", work + "/kernel.bin"])
        if mutant and os.path.exists(mutant):
            os.remove(mutant)
        log.close()
    bad = results.count(False)
    print("%d of %d checks failed" % (bad, len(results)))
    sys.exit(1 if bad or rc else 0)


if __name__ == "__main__":
    main()
