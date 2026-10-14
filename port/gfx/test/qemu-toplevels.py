#!/usr/bin/env python3
"""wlr-foreign-toplevel-management in meechowm, on QEMU (stage 11.7).

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-toplevels)
with the frame buffer driver at virtual=1, as qemu-strip.py does, starts
meechowm with two windows (wlhello -f: red, then green) and asks with
ftlprobe (port/gfx/test/ftlprobe.c, built here: the protocol's client
code by nbwayland-scanner) what a task bar sees and what it can do -
each answer checked against what the compositor itself says (meechowm -m
state):

  1. both windows, each on the one output, green active (it opened last);
  2. "activate red": red has the focus, and is the active one;
  3. "maximize red": red is all of the screen's width, and says so;
     then the width set by a command (meechowm -m width 1/2): not
     maximized any more;
  4. "close green", with a second ftlprobe watching: green asked to close
     goes, and the watcher is told ("closed green");
  5. blue opens and goes to workspace 2 (meechowm -m move down);
     "activate red", which is on workspace 1: the compositor goes there.

-c PATH: the compositor at PATH in the guest instead of /usr/bin/meechowm.
Log of the whole run: ~/qemu-toplevels.log.

    python3 port/gfx/test/qemu-toplevels.py [-k] [-s cores] [-c PATH]
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
WORK = os.path.join(HOME, "obj-evbarm64/work-toplevels")
GUESTDIR = "/usr/tests/gfx"
SCANNER = os.path.join(HOME, "tools-evbarm64/bin/nbwayland-scanner")
XML = os.path.join(HERE, "..", "..", "..", "external/mit/wlroots/dist/"
                   "protocol/wlr-foreign-toplevel-management-unstable-v1.xml")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


qemu_meechowm = load("qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_strip = load("qemu_strip", os.path.join(HERE, "qemu-strip.py"))


def build():
    """ftlprobe, into DESTDIR/usr/tests/gfx."""
    gen = os.path.join(HOME, "obj-evbarm64/ftlprobe")
    os.makedirs(gen, exist_ok=True)
    hdr = os.path.join(gen, "wlr-foreign-toplevel-management-unstable-v1-"
                       "client-protocol.h")
    code = os.path.join(gen, "wlr-foreign-toplevel-management-unstable-v1-"
                        "protocol.c")
    for kind, out in (("client-header", hdr), ("private-code", code)):
        if subprocess.call([SCANNER, kind, XML, out]) != 0:
            sys.exit("wayland-scanner failed: " + kind)
    dest = qemu_meechowm.DEST
    if subprocess.call([qemu_meechowm.XCC, "-O2", "-static", "-Wall",
                        "--sysroot=" + dest, "-I" + gen, "-o",
                        os.path.join(dest, GUESTDIR.lstrip("/"), "ftlprobe"),
                        os.path.join(HERE, "ftlprobe.c"), code,
                        "-lwayland-client", "-lepoll-shim", "-lffi",
                        "-lgfxcompat", "-lmthread", "-lm"]) != 0:
        sys.exit("build failed: ftlprobe")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-c", default="meechowm", help="the compositor")
    a = ap.parse_args()

    if not a.k:
        qemu_meechowm.stage()
        build()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="2400",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-toplevels.log"), "w")
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
        return qemu_strip.parse_state(run("meechowm -m state"))

    def probe(args=""):
        """ftlprobe: {title: (state, outputs)}, and the whole output."""
        out = run("$G/ftlprobe %s" % args)
        seen = {}
        for m in re.finditer(r"toplevel (\S+) app \S+ state (\S+) outputs "
                             r"(\d+)", out):
            seen[m.group(1)] = (m.group(2), int(m.group(3)))
        return seen, out

    def focused(st):
        return [c["title"] for cols in st["cols"].values() for c in cols
                if c["focus"]]

    def wait_for(pred, timeout=150):
        deadline = time.time() + timeout
        while True:
            st = state()
            if pred(st) or time.time() > deadline:
                return st
            time.sleep(2)

    def spawn(title, argb, n):
        send("meechowm -m spawn $G/wlhello -f -t 1800 -c %s -T %s"
             % (argb, title))
        wait_for(lambda s: sum(len(c) for c in s["cols"].values()) == n, 90)

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
        send("export WAYLAND_DISPLAY=wayland-0")
        spawn("red", "0xffc03030", 1)
        spawn("green", "0xff30c030", 2)

        # 1. What a task bar sees.
        seen, out = probe()
        record("1. both windows, on the output, green active",
               seen.get("red") == ("-", 1) and
               seen.get("green") == ("activated", 1),
               out.strip().replace("\n", "; "))

        # 2. Activate red.
        probe("-a red")
        st = wait_for(lambda s: focused(s) == ["red"], 30)
        seen, out = probe()
        record("2. activate red: it has the focus, and is active",
               focused(st) == ["red"] and seen.get("red") ==
               ("activated", 1) and seen.get("green") == ("-", 1),
               "focus %s; %s" % (focused(st), out.strip().replace("\n",
                                                                   "; ")))

        # 3. Maximize red, then a width by command.
        probe("-m red")
        st = wait_for(lambda s: any(c["title"] == "red" and c["width"] == "1"
                                    for c in s["cols"].get(1, [])), 30)
        seen, out = probe()
        red = [c for c in st["cols"].get(1, []) if c["title"] == "red"]
        record("3. maximize red: the screen's width, and it says so",
               red and red[0]["width"] == "1" and
               seen.get("red") == ("activated,maximized", 1),
               "red %s; %s" % (red, out.strip().replace("\n", "; ")))
        run("meechowm -m width 1/2")
        seen, out = probe()
        record("3. a width by command: not maximized any more",
               seen.get("red") == ("activated", 1),
               out.strip().replace("\n", "; "))

        # 4. Close green, watched.
        send("$G/ftlprobe -w 12 > /tmp/watch.txt 2>&1 &")
        time.sleep(4)
        probe("-c green")
        st = wait_for(lambda s: not any(c["title"] == "green"
                                        for cols in s["cols"].values()
                                        for c in cols), 60)
        time.sleep(10)
        watched = run("cat /tmp/watch.txt")
        record("4. close green: it goes",
               not any(c["title"] == "green" for cols in st["cols"].values()
                       for c in cols), "columns %s" % st["cols"])
        record("4. and the watcher is told", "closed green" in watched,
               watched.strip().replace("\n", "; "))

        # 5. Across workspaces.
        spawn("blue", "0xff3030c0", 2)
        run("meechowm -m move down")
        st = wait_for(lambda s: s.get("active") == 2, 30)
        record("5. blue on workspace 2, which is on the screen",
               st.get("active") == 2 and
               [c["title"] for c in st["cols"].get(2, [])] == ["blue"],
               "active %s, columns %s" % (st.get("active"), st["cols"]))
        probe("-a red")
        st = wait_for(lambda s: s.get("active") == 1 and
                      "red" in focused(s), 30)
        seen, out = probe()
        record("5. activate red, on workspace 1: the compositor goes there",
               st.get("active") == 1 and "red" in focused(st) and
               seen.get("red", ("",))[0].startswith("activated"),
               "active %s, focus %s; %s" % (st.get("active"), focused(st),
                                            out.strip().replace("\n", "; ")))

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
