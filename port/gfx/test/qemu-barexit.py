#!/usr/bin/env python3
"""Does the panel go with the compositor?  (stage 11.7, QEMU)

Boots the disk root in its own WORK_DIR (~/obj-evbarm64/work-barexit) with
the frame buffer driver at virtual=1, as qemu-strip.py does, starts
meechowm and in it meechowm-bar - yambar, a thread per module and one for
the bar - and once the panel is a layer of the compositor's, ends the
compositor (meechowm -m quit), which must exit with status 0.  yambar has
lost its display then and must go by itself; the stand looks for it with
ps for -t seconds, and if it is still there prints what it waits on (ps
-l: wait channel) and a few seconds of trace(1) attached to it.

    python3 port/gfx/test/qemu-barexit.py [-k] [-s cores] [-t seconds]
                                          [-r rounds] [-e]

-r: start and end the compositor that many times in one boot; -e: yambar
with GFX_POLL_DEBUG and its log at info, in /tmp/bar.log of the guest.
Log of the whole run: ~/qemu-barexit.log.
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
WORK = os.path.join(HOME, "obj-evbarm64/work-barexit")
GUESTDIR = "/usr/tests/gfx"

_spec = importlib.util.spec_from_file_location(
    "qemu_meechowm", os.path.join(HERE, "qemu-meechowm.py"))
qemu_meechowm = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qemu_meechowm)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-t", type=int, default=20,
                    help="seconds yambar has to go")
    ap.add_argument("-r", type=int, default=1, help="rounds")
    ap.add_argument("-e", action="store_true", help="yambar's own log")
    a = ap.parse_args()

    if not a.k:
        qemu_meechowm.stage()
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="3000",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.path.join(HOME, "qemu-barexit.log"), "w")
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
        return expect(r"RUNDONE", timeout)

    results = []

    def record(name, ok, why=""):
        results.append((name, ok))
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    def show(out):
        for l in out.splitlines()[1:-1]:
            print("      | " + l.rstrip())
        sys.stdout.flush()

    def bar_pids():
        out = run("ps -ax -o pid,command | grep '[y]ambar'")
        return [int(m) for m in re.findall(r"^\s*(\d+)\s+yambar", out, re.M)]

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
        bar = "meechowm-bar"
        if a.e:
            # The configuration meechowm-bar gives it (MEECHO's, made
            # from the palette: minix/share/style), then yambar itself
            # with a log at info level.
            bar = ("GFX_POLL_DEBUG=1 yambar -d info -s "
                   "-c /usr/share/meecho/bar.yml > /tmp/bar.log 2>&1")
        for rnd in range(1, a.r + 1):
            tag = "round %d: " % rnd if a.r > 1 else ""
            # The exit status to a file: ash frees a finished job at the
            # next prompt, and wait $MWM then says 127 (qemu-strip.py).
            send("rm -f /tmp/mwm.rc; (MEECHOWM_DEBUG=1 meechowm > "
                 "/tmp/mwm.log 2>&1; echo $? > /tmp/mwm.rc) & MWM=$!")
            send("n=0; while [ $n -lt 90 ] && ! meechowm -m state >/dev/null "
                 "2>&1; do sleep 2; n=$((n+1)); done; echo WA''ITED $n")
            expect(r"WAITED \d+", 300)
            send("meechowm -m spawn '%s'" % bar)
            deadline = time.time() + 300
            while True:
                out = run("meechowm -m state | grep '^layer'")
                if "panel" in out or time.time() > deadline:
                    break
                time.sleep(3)
            record(tag + "yambar is a panel of the compositor's",
                   "panel" in out, "no panel layer")
            time.sleep(5)		# a script module round or two
            pids = bar_pids()
            record(tag + "one yambar", len(pids) == 1, "%s" % pids)
            send("meechowm -m quit")
            out = run("sleep 4; if kill -0 $MWM 2>/dev/null; then "
                      "echo AL''IVE; else echo GO''NE; fi")
            record(tag + "the compositor ends", "GONE" in out,
                   "it is still running")
            m = re.search(r"RC=(\d+|none)",
                          run("echo RC=$(cat /tmp/mwm.rc 2>/dev/null || "
                              "echo none)"))
            rc = m.group(1) if m else "?"
            record(tag + "with status 0", rc == "0",
                   "status %s%s" % (rc, " (SIGABRT)" if rc == "134" else ""))
            out = run("grep -iE 'assert|abort|ERROR' /tmp/mwm.log | tail -5")
            # Not the command itself, which the terminal echoes twice and
            # which says "assert|abort" too.
            if re.search(r"(?i)assert|abort", "\n".join(
                    l for l in out.splitlines() if "RU''NDONE" not in l)):
                print("      the compositor's last words:")
                show(out)
            t0 = time.time()
            while True:
                left = bar_pids()
                if not left or time.time() - t0 > a.t:
                    break
                time.sleep(2)
            record(tag + "yambar goes with the compositor (%d s)" % a.t,
                   not left, "still there: %s" % left)
            if left:
                print("      ps -l:")
                show(run("ps -alx | sed -n '1p;/[y]ambar/p;/[m]eechowm/p;"
                         "/[a]wk/p;/[s]h /p'"))
                print("      trace -p %d, 5 s:" % left[0])
                show(run("trace -p %d -o /tmp/tr.txt & T=$!; sleep 5; "
                         "kill -INT $T; sleep 2; tail -30 /tmp/tr.txt"
                         % left[0], 60))
                if a.e:
                    print("      /tmp/bar.log:")
                    show(run("tail -30 /tmp/bar.log"))
                run("kill %s; sleep 3" % " ".join(map(str, left)))
                left = bar_pids()
                record(tag + "SIGTERM ends it", not left,
                       "still there: %s" % left)
                if left:
                    run("kill -9 %s" % " ".join(map(str, left)))
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
