#!/usr/bin/env python3
"""Boot the disk root on QEMU, run commands as root, print what they say.

    python3 port/test/qemu-run.py [-w WORK] [-k] [-s cores] [-t seconds]
                                  'command' ['command' ...]

Each command runs in turn at the shell; its output is printed between
"== command" lines, and the exit status after it.  -w names the WORK_DIR
under ~/obj-evbarm64 (default work-run), so that this can run beside
another session's QEMU; -k keeps that directory's disk image (default: made
fresh from the ramdisk and DESTDIR).  The exit status is 0 when every
command exited 0.  Log: ~/qemu-run.log, or $QEMU_RUN_LOG (sessions running
side by side would write over each other's).
"""

import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
RAMIMAGE = os.path.join(HERE, "..", "ramimage.sh")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-w", default="work-run", help="WORK_DIR name")
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-t", type=int, default=600, help="seconds per command")
    ap.add_argument("cmd", nargs="+")
    a = ap.parse_args()

    work = os.path.join(HOME, "obj-evbarm64", a.w)
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ, WORK_DIR=work, QEMU_TIMEOUT="3600",
               QEMU_SMP=str(a.s))
    if not a.k:
        env["DISK_FRESH"] = "1"
    log = open(os.environ.get("QEMU_RUN_LOG",
                              os.path.join(HOME, "qemu-run.log")), "w")
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
                return out, m
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

    bad = 0
    try:
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("PATH=$PATH:/sbin:/usr/sbin; export PATH")
        expect(r"# ", 20)
        for i, cmd in enumerate(a.cmd):
            print("== %s" % cmd)
            sys.stdout.flush()
            send("%s; echo RUN%d''END=$?" % (cmd, i))
            out, m = expect(r"RUN%dEND=(\d+)" % i, a.t)
            body = out.split("\n", 1)[1] if "\n" in out else out
            print(re.sub(r"RUN%dEND=\d+$" % i, "", body).rstrip())
            print("-- exit %s" % m.group(1))
            if m.group(1) != "0":
                bad += 1
        send("/sbin/poweroff")
        time.sleep(4)
    except RuntimeError as e:
        print("FAIL: %s" % e)
        bad += 1
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", work + "/kernel.bin"])
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
