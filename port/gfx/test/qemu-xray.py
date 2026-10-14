#!/usr/bin/env python3
"""The x-ray view on QEMU (stage 11.7): the desktop's windows as processes,
the lines between them as their messages - checked against the kernel.

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-xray-desk)
with the frame buffer at virtual=1, as qemu-shot.py does, on the KTRACE
variant of the kernel (port/kbuild-variant.sh ktrace "-O2 -DKTRACE=1",
booted by KERNEL_ELF: the shared kernel stays the one everybody boots).
meechowm with two windows - wlhello -f -q, red and blue: -q waits for the
compositor without a timeout, so that between two events a window sends
nothing at all.  Then, each step against something that does not share
its code:

  pid       the windows' pids in "meechowm -m state", which the compositor
            has from the credentials of each client's socket (uds,
            LOCAL_PEEREID), are the pids the shell started them with ($!)
  exact     for each window: what the x-ray draws from it - the sum of its
            lines, "meechowm-xray -d", made from /proc/<pid>/ipc, the
            kernel's message matrix - less what the kernel sent in its name
            (page faults, the end of a quantum) grows between two moments
            exactly as much as its row of /proc/ktrace (ipc + kcall), which
            the kernel counts elsewhere: where a process enters it, not
            where a message leaves.  Between the moments the windows are
            resized and refocused (meechowm -m width, focus), so that they
            draw; a moment is taken when two dumps around the read of
            /proc/ktrace agree, i.e. when no window sent anything meanwhile.
  picture   Super+x: the x-ray's layer surface is up (state), and on the
            screen (fbart's frame, through /dev/fb0) are the windows'
            frames, gone back, where -d lays them out, and the kernel's bar
            along the bottom; the frame goes to a PNG (-o)
  click     a click (usb_mouse inject=) on the red window drawn back puts
            up red's passport (state: passport PID windows 1)
  passport  Super+x again closes the view; "meechowm -m passport PID" for
            blue answers with blue's program, pid and user and whom it
            talks to, and its frame (STYLE_ACCENT) is on the screen at the
            window's corner; the same again puts it away
  flash     "minix-service refresh lwip" while the x-ray samples: lwip's
            node flashes (-d says "flash lwip")

    python3 port/gfx/test/qemu-xray.py [-K kernel|shared] [-s cores]
                                       [-o file.png] [-x]

-K shared boots the shared kernel, which has no /proc/ktrace: "exact"
is skipped.  -x: stop at the first failure.  Log: ~/qemu-xray.log.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-xray-desk")
KTRACE_KERNEL = os.path.join(HOME, "obj-kvar/ktrace/minix/kernel/kernel")
GUESTDIR = "/usr/tests/gfx"
W, H = 1280, 720

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


# The snapshot, as a file in the guest: the tty cuts lines at about 255.
SNAP = r"""#!/bin/sh
# snap.sh TAG: the x-ray's lines and /proc/ktrace's rows of one moment -
# the read of /proc/ktrace between two dumps that agree on every window.
t=$1; n=0
while :; do
	meechowm-xray -d | grep -E '^(window|line|sum) ' > /tmp/xr1
	grep '^proc ' /proc/ktrace > /tmp/kt
	meechowm-xray -d | grep -E '^(window|line|sum) ' > /tmp/xr2
	cmp -s /tmp/xr1 /tmp/xr2 && break
	n=$((n + 1))
	[ $n -gt 20 ] && { echo "SNAP $t never settled"; break; }
	sleep 1
done
printf 'SNAP%s %s\n' -BEGIN "$t"
cat /tmp/xr1 /tmp/kt
printf 'SNAP%s %s\n' -END "$t"
"""


def parse_snap(text, tag):
    m = re.search(r"SNAP-BEGIN %s\r?\n(.*?)SNAP-END %s" % (tag, tag), text,
                  re.S)
    if not m:
        raise RuntimeError("no snapshot %s" % tag)
    wins, kt = {}, {}
    for l in m.group(1).splitlines():
        w = l.split()
        if not w:
            continue
        if w[0] == "window" and len(w) >= 4 and w[3] != "-":
            wins.setdefault(int(w[1]), {"slot": int(w[3]), "lines": {},
                                        "sent": 0, "sum": 0, "kernel": 0})
        elif w[0] == "line" and len(w) >= 5:
            wins.setdefault(int(w[1]), {"slot": -1, "lines": {}, "sent": 0,
                                        "sum": 0, "kernel": 0})
            wins[int(w[1])]["lines"][w[2] + "/" + w[3]] = int(w[4])
        elif w[0] == "sum" and len(w) == 5:
            d = wins.setdefault(int(w[1]), {"slot": -1, "lines": {}})
            d["sent"], d["sum"], d["kernel"] = int(w[2]), int(w[3]), \
                int(w[4])
        elif w[0] == "proc" and len(w) == 5:
            kt[int(w[1])] = (int(w[3]), int(w[4]))
    return wins, kt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-K", default=KTRACE_KERNEL)
    ap.add_argument("-s", type=int, default=1)
    ap.add_argument("-o", default=os.path.join(HOME, "meecho-xray.png"))
    ap.add_argument("-x", action="store_true")
    ap.add_argument("--only", default="",
                    help="steps to run, of exact,picture,passport,flash,cost "
                    "(picture includes click)")
    a = ap.parse_args()

    def want(step):
        return not a.only or step in a.only.split(",")
    ktrace = a.K != "shared"
    st = style()

    qemu_meechowm.stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="3600",
               QEMU_SMP=str(a.s), DISK_FRESH="1")
    if ktrace:
        if not os.path.exists(a.K):
            sys.exit("no %s: port/kbuild-variant.sh ktrace \"-O2 "
                     "-DKTRACE=1\" first" % a.K)
        env["KERNEL_ELF"] = a.K
    log = open(os.path.join(HOME, "qemu-xray.log"), "w")
    p = subprocess.Popen(["bash", RAMIMAGE, "-d"], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         bufsize=0, env=env, start_new_session=True)
    buf = ""
    results = []

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
                # printf's format: its backslashes and percents doubled,
                # the shell's quotes closed around a quote.
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

    def state_has(pat, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            out = state()
            if re.search(pat, out):
                return out
            time.sleep(2)
        return None

    def shot():
        send("dd if=/dev/fb0 bs=5120 count=%d 2>/dev/null | gzip -9 | "
             "uuencode -m shot.gz; echo SH''OT" % H)
        out, _ = expect(r"====\s*\r?\n.*SHOT", 900)
        m = re.search(r"begin-base64 \d+ shot\.gz\r?\n(.*?)\r?\n====", out,
                      re.S)
        if not m:
            raise RuntimeError("no picture in the output")
        raw = gzip.decompress(base64.b64decode(
            "".join(l.strip() for l in m.group(1).splitlines())))
        if len(raw) < W * H * 4:
            raise RuntimeError("%d bytes, not %d" % (len(raw), W * H * 4))
        return raw

    def px(raw, x, y):
        o = (y * W + x) * 4
        return raw[o + 2] << 16 | raw[o + 1] << 8 | raw[o]

    def snap(tag):
        send("sh /tmp/snap.sh %s" % tag)
        out, _ = expect(r"SNAP-END %s\r?\n" % tag, 600)
        if "never settled" in out:
            print("      (%s: the windows never fell quiet)" % tag)
        return parse_snap(out, tag)

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
        put_file("/tmp/snap.sh", SNAP)

        # pid
        send("$G/wlhello -f -q -c 0xffd04040 -T red > /tmp/red.log 2>&1 & "
             "echo PI''D red $!")
        _, m = expect(r"PID red (\d+)\r?\n", 60)
        red = int(m.group(1))
        state_has(r"column 1\.1 .* pid %d " % red, 300)
        send("$G/wlhello -f -q -c 0xff3a7bd5 -T blue > /tmp/blue.log 2>&1 & "
             "echo PI''D blue $!")
        _, m = expect(r"PID blue (\d+)\r?\n", 60)
        blue = int(m.group(1))
        out = state_has(r"column 1\.2 .* pid %d " % blue, 300)
        pids = {int(x) for x in re.findall(r"^column .* pid (\d+) ", out or "",
                                            re.M)}
        check("pid", pids == {red, blue}, "state says %s, the shell %s" %
              (sorted(pids), sorted({red, blue})))

        if want("exact"):
            # exact
            time.sleep(3)
            wa, ka = snap("a")
            send("meechowm -m width next; meechowm -m focus left; "
                 "meechowm -m width next; meechowm -m focus right")
            time.sleep(4)
            wb, kb = snap("b")
            for name, pid in (("red", red), ("blue", blue)):
                if pid not in wa or pid not in wb:
                    check("exact", False, "%s (%d) not in a dump" % (name, pid))
                    continue
                slot = wb[pid]["slot"]
                dl = wb[pid]["sum"] - wa[pid]["sum"]
                dk = wb[pid]["kernel"] - wa[pid]["kernel"]
                whole = wb[pid]["sum"] == wb[pid]["sent"]
                lines = ", ".join("%s +%d" % (k.split("/")[0],
                                  v - wa[pid]["lines"].get(k, 0))
                                  for k, v in sorted(wb[pid]["lines"].items())
                                  if v != wa[pid]["lines"].get(k, 0))
                if not ktrace:
                    check("exact", dl > 0 and whole, "%s: lines +%d (%s); no "
                          "KTRACE to compare with" % (name, dl, lines))
                    continue
                if slot not in ka or slot not in kb:
                    check("exact", False, "%s: no /proc/ktrace row for slot %d"
                          % (name, slot))
                    continue
                dkt = (kb[slot][0] - ka[slot][0]) + (kb[slot][1] - ka[slot][1])
                check("exact", dl > 0 and whole and dl - dk == dkt,
                      "%s (pid %d, slot %d): lines +%d - kernel +%d = %d; "
                      "/proc/ktrace +%d%s  [%s]" % (name, pid, slot, dl, dk,
                      dl - dk, dkt, "" if whole else "; lines != row",
                      lines))

        if want("picture"):
            # picture: and a window that is not quiet - wlhello without -q
            # looks at the clock five times a second - for lines from a window
            send("$G/wlhello -f -t 3600 -c 0xff40a060 -T green > /tmp/green.log "
                 "2>&1 & echo PI''D green $!")
            _, m = expect(r"PID green (\d+)\r?\n", 60)
            green = int(m.group(1))
            state_has(r"column 1\.\d .* pid %d " % green, 300)
            send("meechowm -m xray")
            state_has(r"layer meechowm-xray ", 300)
            time.sleep(6)		# two samples: the rates, and so the lines
            send("meechowm-xray -d -n 2 > /tmp/xd; grep -E "
                 "'^(wrect|node|edge|line) ' /tmp/xd | head -120; echo XD''ONE")
            out, _ = expect(r"XDONE\r?\n", 600)
            rects = {int(m.group(1)): tuple(int(m.group(i)) for i in (2, 3, 4, 5))
                     for m in re.finditer(r"^wrect (\d+) (\d+) (\d+) (\d+) "
                                          r"(\d+)", out, re.M)}
            kern = re.search(r"^node kernel \S+ \S+ kernel \S+ \S+ (\d+) (\d+) "
                             r"(\d+) (\d+)", out, re.M)
            nodes = re.findall(r"^node (\S+) ", out, re.M)
            edges = len(re.findall(r"^edge ", out, re.M))
            raw = shot()
            qemu_shot.png(a.o, raw, W, H)
            print("      the screen: %s" % a.o)
            bad = []
            for pid, (x, y, w, h) in rects.items():
                c = px(raw, x + w // 2, y)
                if c not in (st["ACCENT"], st["ACCENT_DEEP"]):
                    bad.append("%d's frame 0x%06x" % (pid, c))
            if kern:
                x, y, w, h = (int(kern.group(i)) for i in (1, 2, 3, 4))
                c = px(raw, x + w // 2, y)
                if c != st["MAGENTA"]:
                    bad.append("the kernel's bar 0x%06x" % c)
            # What the view costs: its own row of the matrix, ten seconds
            # apart - messages it sends, by receiver (informational).
            put_file("/tmp/cost.sh", "for d in /proc/[0-9]*; do set -- $(cat "
                     "$d/psinfo 2>/dev/null); [ \"$4\" = meechowm-xray ] && "
                     "P=${d#/proc/}; done\ngrep '^to' /proc/$P/ipc > /tmp/c1\n"
                     "sleep 10\ngrep '^to' /proc/$P/ipc > /tmp/c2\n"
                     "echo COST pid $P $(awk '{ n += $6 } END { print n }' "
                     "/tmp/c1) $(awk '{ n += $6 } END { print n }' /tmp/c2)")
            send("sh /tmp/cost.sh")
            cost, cm = expect(r"COST pid (\d+) (\d+) (\d+)\r?\n", 120)
            print("      the view itself: pid %s, %.0f messages/s" %
                  (cm.group(1), (int(cm.group(3)) - int(cm.group(2))) / 10.0))
            glines = re.findall(r"^line %d \S+ \S+ \d+ \S+ ([\d.]+)" % green,
                                out, re.M)
            check("picture", len(rects) >= 2 and kern is not None and
                  edges > 0 and not bad and any(float(x) > 0 for x in glines),
                  "%d windows gone back, %d edges, green's lines %s, nodes "
                  "%s%s" % (len(rects), edges, " ".join(glines),
                            " ".join(nodes), "; " + ", ".join(bad) if bad else ""))

        if want("picture"):
            # click: into the top left corner, then to the middle of the window
            # that shows largest, and press
            target = max(rects, key=lambda k: rects[k][2] * rects[k][3]) \
                if rects else None
            if target is not None:
                x, y, w, h = rects[target]
                tx, ty = x + w // 2, y + h // 2
                moves = ["0:-127:-127"] * 12
                while tx > 0 or ty > 0:
                    dx, dy = min(tx, 127), min(ty, 127)
                    moves.append("0:%d:%d" % (dx, dy))
                    tx, ty = tx - dx, ty - dy
                moves += ["1:0:0", "0:0:0"]
                put_file("/tmp/click.sh", "minix-service up /service/usb_mouse "
                         "-label mousex -args 'inject=%s'" % ",".join(moves))
                send("sh /tmp/click.sh; sleep 8; minix-service down mousex; "
                     "echo CL''ICK")
                expect(r"CLICK\r?\n", 120)
                out = state_has(r"passport %d windows 1" % target, 60)
                check("click", out is not None, "%d clicked at its middle drawn "
                      "back: %s" % (target, "passport up" if out else
                                    "no passport"))
            else:
                check("click", False, "no window laid out")

        if want("passport"):
            # passport
            send("meechowm -m xray")
            out = state_has(r"^(?!.*layer meechowm-xray )", 120)
            out = state()
            closed = "layer meechowm-xray" not in out
            # A window wholly on the screen, so that its corner can be seen:
            # (y, at, width, pid) of each column of the active workspace.
            cols = [(int(c[0]), int(c[1]), int(c[2]), int(c[3])) for c in
                    re.findall(r"^column 1\.\d+ \S+ (\d+) (\d+) \S+ width \S+ "
                               r"\S+ at (-?\d+) size \S+ pid (\d+) ", out, re.M)]
            cols = [(y, at, w, pid) for (y, w, at, pid) in cols
                    if at >= 0 and at + w <= W]
            blue = cols[0][3] if cols else blue
            send("meechowm -m passport %d; echo PA''SS" % blue)
            out, _ = expect(r"PASS\r?\n", 120)
            said = re.search(r"passport %d\r?\n(\S+)  pid %d  (\S+)\r?\n"
                             r"memory [\d.]+ MiB.*?\r?\nto  (.*?)\r?\n" %
                             (blue, blue), out, re.S)
            out = state()
            col = re.search(r"^column \S+ \S+ (\d+) \S+ \S+ width \S+ \S+ at "
                            r"(-?\d+) size \S+ pid %d " % blue, out, re.M)
            up = re.search(r"passport %d windows 1" % blue, out) is not None
            c = None
            if col:
                time.sleep(3)
                raw = shot()
                pp = a.o.replace(".png", "") + "-passport.png"
                qemu_shot.png(pp, raw, W, H)
                print("      the passport: %s" % pp)
                # The passport's border, 2 wide, PAD (6) into the window.
                c = px(raw, int(col.group(2)) + 6 + 20, int(col.group(1)) + 6)
                rows = [y for y in range(int(col.group(1)), int(col.group(1)) +
                        40) if px(raw, int(col.group(2)) + 26, y) == st["ACCENT"]]
                print("      accent at x %d, rows %s" %
                      (int(col.group(2)) + 26, rows))
            check("passport", closed and said is not None and up and
                  said.group(1) == "wlhello" and "vfs" in said.group(3) and
                  c == st["ACCENT"], "view closed %s; says %s; frame 0x%s" %
                  (closed, said.group(0).replace("\r", "").replace("\n", " | ")
                   if said else "nothing", "%06x" % c if c is not None else "?"))
            send("meechowm -m passport %d; echo PA''SS" % blue)
            expect(r"PASS\r?\n", 120)
            out = state()
            check("passport", "passport %d" % blue not in out, "the same again "
                  "puts it away")

        if want("flash"):
            # flash: lwip restarted once the x-ray has its first sample (which
            # under TCG takes a while - fontconfig), and seen by a later one:
            # the x-ray samples a second apart until a flash shows or a minute
            # has gone, however long the refresh and the samples take.
            put_file("/tmp/flash.sh", "grep endpoint /proc/service/lwip\n"
                     "meechowm-xray -d -n 90 -i 1000 > /tmp/xf 2>&1 &\nP=$!\n"
                     "while ! grep -q 'XRAY end' /tmp/xf; do sleep 1; done\n"
                     "minix-service refresh lwip\necho refreshed $?\n"
                     "grep -E 'endpoint|restarts' /proc/service/lwip\nn=0\n"
                     "while ! grep -q '^flash lwip' /tmp/xf && [ $n -lt 60 ]; do "
                     "sleep 1; n=$((n+1)); done\nkill $P\n"
                     "echo samples $(grep -c 'XRAY end' /tmp/xf)\n"
                     "grep -E '^(flash|service lwip)' /tmp/xf | uniq -c\n"
                     "grep -v -E '^(window|wrect|line|sum|node|edge|XRAY|service)' "
                     "/tmp/xf | head -5")
            send("sh /tmp/flash.sh; echo FL''ASH")
            out, _ = expect(r"FLASH\r?\n", 600)
            check("flash", re.search(r"^flash lwip ", out, re.M) is not None,
                  "after refresh lwip: %s" % (", ".join(re.findall(
                      r"^flash (\S+) ", out, re.M)) or "no flash"))

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
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
        log.close()
    bad = results.count(False)
    print("%d of %d checks failed" % (bad, len(results)))
    sys.exit(1 if bad or rc else 0)


if __name__ == "__main__":
    main()
