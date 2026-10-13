#!/usr/bin/env python3
"""ssh with a terminal on QEMU: which pseudo terminal a session gets.

Unix98 ptys (/dev/ptmx, slaves on ptyfs at /dev/pts) work since stage 11.4,
and openpty(3) prefers them to the old /dev/ttypN pairs - so sshd now gives
a session /dev/pts/N.  This boots the disk root in its own WORK_DIR
(~/obj-evbarm64/work-pts), starts sshd as rc of the board does (no -E, the
default log level), and logs in from the host three times with a terminal
(ssh -tt), each asking tty(1) and doing a little with it: the answers must
be /dev/pts/N, and a command run as root must work through it.  A fourth
login without a terminal checks the plain path.  Log: ~/qemu-pts.log.

    python3 port/test/ssh/qemu-pts.py

Key: ~/.ssh/qemu_meecho (see qemu-two.sh); host port 2231.
"""

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
WORK = os.path.join(HOME, "obj-evbarm64/work-pts")
KEY = os.path.join(HOME, ".ssh/qemu_meecho")
PORT = "2231"


def main():
    os.makedirs(WORK, exist_ok=True)
    env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="1200",
               QEMU_HOSTFWD=PORT, DISK_FRESH="1")
    log = open(os.path.join(HOME, "qemu-pts.log"), "w")
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
                buf = buf[m.end():]
                return m
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

    def ssh(cmd, tty):
        args = ["ssh", "-i", KEY, "-p", PORT, "-o", "BatchMode=yes",
                "-o", "UserKnownHostsFile=/dev/null",
                "-o", "StrictHostKeyChecking=no", "-o", "ConnectTimeout=60"]
        if tty:
            args.append("-tt")
        r = subprocess.run(args + ["root@127.0.0.1", cmd],
                           stdin=subprocess.DEVNULL, capture_output=True,
                           timeout=300)
        out = r.stdout.decode(errors="replace").replace("\r", "")
        log.write("\n### ssh %s %r -> %d\n%s\n%s\n" % (
            "-tt" if tty else "", cmd, r.returncode, out,
            r.stderr.decode(errors="replace")))
        log.flush()
        return r.returncode, out

    results = []

    def record(name, ok, why=""):
        results.append(ok)
        print("%s  %s%s" % ("ok  " if ok else "FAIL", name,
                            "" if ok else ": " + why))
        sys.stdout.flush()

    try:
        pub = open(KEY + ".pub").read().strip()
        expect(r"login:", 300)
        send("root")
        expect(r"# ", 60)
        send("mkdir -p -m 700 /root/.ssh; echo '%s' > "
             "/root/.ssh/authorized_keys; chmod 600 "
             "/root/.ssh/authorized_keys" % pub)
        send("[ -f /etc/ssh/ssh_host_ed25519_key ] || ssh-keygen -q -t "
             "ed25519 -N '' -f /etc/ssh/ssh_host_ed25519_key; "
             "/usr/sbin/sshd; echo SS''HD $?")
        m = expect(r"SSHD (\d+)", 300)
        record("sshd started", m.group(1) == "0", "exit %s" % m.group(1))
        time.sleep(5)
        for i in range(3):
            rc, out = ssh("tty; echo TTYOK; ls -l `tty`", True)
            t = re.search(r"^(/dev/\S+)$", out, re.M)
            record("login %d with a terminal: %s" % (i + 1,
                   t.group(1) if t else "?"),
                   rc == 0 and t is not None and
                   t.group(1).startswith("/dev/pts/") and "TTYOK" in out,
                   "rc %d, %r" % (rc, out[:200]))
        rc, out = ssh("echo PLAIN-OK; tty", False)
        record("login without a terminal", rc != 255 and "PLAIN-OK" in out,
               "rc %d, %r" % (rc, out[:200]))
        send("ls -l /dev/pts; /sbin/poweroff")
        time.sleep(5)
    except (RuntimeError, subprocess.TimeoutExpired) as e:
        record("the run", False, str(e))
    finally:
        try:
            os.killpg(os.getpgid(p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
    bad = results.count(False)
    print("\n%d check(s), %d failed" % (len(results), bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
