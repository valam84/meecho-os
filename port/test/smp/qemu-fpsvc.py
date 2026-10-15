#!/usr/bin/env python3
"""FP/SIMD state with FP-free services, on QEMU.

Since O8 the system services are built -mgeneral-regs-only and libminc's
getcontext() no longer carries d8-d15 (share/mk/minix.service.mk,
minix/lib/libc/arch/aarch64/sys/ucontext.S). What that can break:

  - a user process's registers across switches to services that no longer
    take the register file: fpstress, while find(1) keeps VFS's worker
    threads switching - the load that once lost VFS's own d15;
  - parent and child save areas: fpfork;
  - libc's getcontext(), which must still carry d8-d15: ctxfp;
  - VFS itself, which must stay up through all of it.

Builds the three probes (static, DESTDIR's libc) into DESTDIR/usr/tests/fp,
boots the disk root in its own WORK_DIR and runs them.

    python3 port/test/smp/qemu-fpsvc.py [-k] [-2] [-3] [-s N]

Log: ~/qemu-fpsvc.log. What QEMU cannot show: anything about caches, and
FP state carried between real cores (port/PORTING-LOG.md, "О8").
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
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.environ.get("WORK_DIR", os.path.join(HOME, "obj-evbarm64/work-fpsvc"))
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/fp"
DOWN = r"panic|assert|MEECHO will now|kernel stack overflow|died"
PROBES = {
    "fpstress": os.path.join(HERE, "fpstress.c"),
    "fpfork": os.path.join(HERE, "..", "dyn", "fpfork.c"),
    "ctxfp": os.path.join(HERE, "..", "libc", "ctxfp.c"),
}


def build():
    out = os.path.join(DEST, GUESTDIR.lstrip("/"))
    os.makedirs(out, exist_ok=True)
    for name, src in PROBES.items():
        r = subprocess.call([XCC, "-O2", "-static", "-Wall",
                             "--sysroot=" + DEST, "-o",
                             os.path.join(out, name), src])
        if r != 0:
            sys.exit("build failed: " + name)
    os.chmod(out, 0o755)


class Guest(object):
    def __init__(self, log, args):
        env = dict(os.environ)
        env["WORK_DIR"] = WORK
        env["QEMU_TIMEOUT"] = "1500"
        env["QEMU_SMP"] = str(args.s)
        if not args.k:
            env["DISK_FRESH"] = "1"
        cmd = ["bash", RAMIMAGE, "-d"]
        if args.el2:
            cmd.append("-2")
        if args.gic3:
            cmd.append("-3")
        self.log = log
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, bufsize=0,
                                  env=env, start_new_session=True)
        self.buf = ""

    def expect_re(self, pattern, timeout):
        deadline = time.time() + timeout
        while True:
            m = re.search(pattern, self.buf)
            if m:
                out = self.buf[:m.end()]
                self.buf = self.buf[m.end():]
                return out, m
            if time.time() >= deadline:
                raise RuntimeError("no %r in %.0f s" % (pattern, timeout))
            if self.p.poll() is not None:
                raise RuntimeError("QEMU exited waiting for %r" % pattern)
            if not select.select([self.p.stdout], [], [], 0.2)[0]:
                continue
            ch = self.p.stdout.read(1)
            if not ch:
                continue
            c = ch.decode(errors="replace")
            self.buf += c
            self.log.write(c)
            self.log.flush()

    def send(self, line):
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()
        time.sleep(0.35)

    def stop(self):
        try:
            os.killpg(os.getpgid(self.p.pid), signal.SIGKILL)
        except Exception:
            pass
        # timeout(1) puts QEMU in a process group of its own.
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-2", dest="el2", action="store_true", help="EL2 entry")
    ap.add_argument("-3", dest="gic3", action="store_true", help="GICv3")
    ap.add_argument("-s", type=int, default=1, help="cores")
    ap.add_argument("-p", type=int, default=300, help="fpstress passes")
    a = ap.parse_args()

    if not a.k:
        build()
    os.makedirs(WORK, exist_ok=True)
    log = open(os.path.join(HOME, "qemu-fpsvc.log"), "w")
    g = Guest(log, a)
    bad = 0

    def shell(cmd, timeout=300):
        g.buf = ""
        g.send(cmd + "; echo EN''D=$?")
        out, m = g.expect_re(r"END=(\d+)|" + DOWN, timeout)
        if not m.group(1):
            raise RuntimeError("the system went down: %r" % m.group(0))
        return out, int(m.group(1))

    def check(name, cmd, want, timeout=300):
        nonlocal bad
        out, r = shell(cmd, timeout)
        ok = r == 0 and re.search(want, out)
        print("%s %s" % ("ok  " if ok else "FAIL", name))
        if not ok:
            bad += 1
            for line in out.splitlines()[-6:]:
                print("     " + line)

    try:
        g.expect_re(r"login:", 300)
        g.send("root")
        g.expect_re(r"# ", 60)
        d = GUESTDIR
        check("ctxfp: libc getcontext keeps d8-d15",
              "%s/ctxfp" % d, r"ctxfp: ok")
        check("fpfork: parent and child FP areas", "%s/fpfork" % d,
              r"fpfork")
        # Two copies per core of fpstress, and find(1) keeping VFS busy
        # beside them: VFS switches worker threads on every lookup, which
        # is where d8-d15 used to matter, and every switch to a service
        # used to hand the register file over.
        n = 2 * a.s
        # A file, line by line: the tty cuts lines near 255 bytes. Waits
        # for the fpstress jobs by pid - a bare wait would wait for find.
        script = [
            "rm -f /tmp/fps.*",
            "(while :; do find / > /dev/null 2>&1; done) &",
            "F=$!",
            "P=",
            "i=0",
            "while [ $i -lt %d ]; do" % n,
            "(%s/fpstress %d > /tmp/fps.$i 2>&1; echo rc=$? >> /tmp/fps.$i) &"
            % (d, a.p),
            "P=\"$P $!\"",
            "i=$((i+1))",
            "done",
            "wait $P",
            "kill $F",
            "cat /tmp/fps.* | sort | uniq -c",
        ]
        g.send("rm -f /tmp/fpsvc.sh")
        for line in script:
            g.send("echo '%s' >> /tmp/fpsvc.sh" % line)
        out, r = shell("sh /tmp/fpsvc.sh", 1500)
        oks = len(re.findall(r"fpstress: ok", out))
        rcs = re.findall(r"(\d+) rc=0", out)
        good = sum(int(x) for x in rcs)
        ok = good == n and "fpstress: pid" not in out
        print("%s fpstress x%d beside find /: %s rc=0 of %d" %
              ("ok  " if ok else "FAIL", n, good, n))
        if not ok:
            bad += 1
            for line in out.splitlines()[-8:]:
                print("     " + line)
        out, r = shell("ls / | wc -l")
        print("%s the system answers after it" % ("ok  " if r == 0 else "FAIL"))
        bad += r != 0
        g.send("/sbin/poweroff")
        time.sleep(3)
    except RuntimeError as e:
        print("FAIL the run: %s" % e)
        bad += 1
    finally:
        g.stop()
    print("%s: %d failed" % ("PASS" if bad == 0 else "FAIL", bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
