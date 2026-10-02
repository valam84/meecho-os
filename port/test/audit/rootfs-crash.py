#!/usr/bin/env python3
"""Kill the root file system server; the system must not hang silently.

The root MFS (label fs_c0d0) is started by the ramdisk /etc/rc through RS,
and RS is its signal manager. Before 2026-10-02 RS restarted it after a
crash, reading the binary back through VFS - from the very file system that
had died - and the machine stopped answering with nothing on the console but
the kernel's "mfs <ep> pc ..." line. With an in-memory copy (bin_img=1) the
restart got further and lost the mount all the same.

The verdict is not "the system survives": VFS cannot take a new root FS
instance under its open vnodes. It is "the machine says what happened and
does not wait forever": the console names the root file system and the
machine stops (verdict "machine stopped"; "SILENT HANG" fails).

How to kill it: PM refuses signals to system processes even from root
(-s is kept for a build where that changes), so
  -r          minix-service refresh fs_c0d0 (SIGTERM from RS, a clean exit);
  -c COMMAND  a crash mid-request: e.g. /usr/tests/journal/flushcache against
              a build with the libminixfs fix of 5fadae3d1 reverted.
  -g          on a hang, dump the process table and RS/VFS stacks via gdb.
Boot arguments go through BOOTARGS (e.g. "rootdevname=c0d0 console=tty00
bin_img=1 rs_verbose=1").

    python3 port/test/audit/rootfs-crash.py [-r | -c CMD | -s SIG] [-g] [-T s]

Log: ~/rootfs-crash.log.
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "qemu_tests", os.environ.get("QT", os.path.join(HERE, "..", "suite", "qemu-tests.py")))
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
qt.TESTDIR = "/root"

LOUD = r"KILLED|root file system|panic|MEECHO will now|halted|RS:|mfs \d+ pc|SIG"


def gdb_dump():
    """Process table, then a return-address scan of RS and VFS stacks,
    resolved against their binaries (port/test/gdb)."""
    gd = os.path.join(HERE, "..", "gdb")
    obj = os.path.expanduser("~/obj-evbarm64/minix")
    kern = os.path.join(obj, "kernel", "kernel")
    base = ["gdb-multiarch", "-batch", "-nx", "-ex", "file " + kern,
            "-ex", "target remote :1234"]
    out = subprocess.run(base + ["-x", os.path.join(gd, "pdump.py"),
                                 "-ex", "detach"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=120).stdout.decode(errors="replace")
    slots = {}
    for line in out.splitlines():
        m = re.match(r"\s*(\d+) (\S+)\s+ep=", line)
        if m:
            print("  " + line)
            slots.setdefault(m.group(2), int(m.group(1)))
    for name, binary in (("rs", "servers/rs/rs"), ("vfs", "servers/vfs/vfs")):
        if name not in slots:
            continue
        s = slots[name]
        subprocess.run(base + ["-x", os.path.join(gd, "ustack.py"),
                               "-ex", "python scan(%d)" % s, "-ex", "detach"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=120, cwd="/tmp")
        try:
            addrs = open("/tmp/scan-%d.txt" % s).read().split()
        except OSError:
            continue
        res = subprocess.run(["bash", os.path.join(gd, "symall.sh"),
                              os.path.join(obj, binary)] + addrs,
                             stdout=subprocess.PIPE).stdout.decode()
        seen = []
        for line in res.splitlines():
            f = line.split()[1].split("+")[0]
            if f not in seen:
                seen.append(f)
        print("  %s stack: %s" % (name, " <- ".join(seen[:20])))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-s", default="SEGV", help="signal to send")
    ap.add_argument("-T", type=int, default=90, help="seconds to wait")
    ap.add_argument("-r", action="store_true",
                    help="minix-service refresh instead of a signal (PM "
                    "refuses signals to system processes even from root)")
    ap.add_argument("-c", help="a shell command that crashes the root FS "
                    "(e.g. a probe against a build with the bug put back)")
    ap.add_argument("-g", action="store_true",
                    help="on a hang, dump the kernel process table and the "
                    "RS and VFS stacks through QEMU's gdbstub")
    a = ap.parse_args()

    log = open(os.path.expanduser("~/rootfs-crash.log"), "w")
    if a.g:
        os.environ["QEMU_EXTRA"] = (os.environ.get("QEMU_EXTRA", "") +
                                    " -gdb tcp::1234").strip()
    g = qt.Guest(log, timeout=1200)
    g.login()
    g.send("export PATH=$PATH:/sbin:/usr/sbin")
    g.expect("# ", 20)

    # The root FS is the RS-started mfs whose arguments name c0d0.
    g.buf = ""
    g.send("ps ax | grep '[m]fs'; echo PS''-END")
    out = g.expect("PS-END", 30)
    out += g.expect("# ", 10)
    print(out.strip())
    pid = None
    for line in out.splitlines():
        if "c0d0" in line and "grep" not in line:
            m = re.match(r"\s*(\d+)", line)
            if m:
                pid = m.group(1)
    if pid is None:
        print("no root mfs in ps output")
        g.kill_group()
        return 2
    print("root mfs pid %s, %s" % (pid, a.c or ("refresh" if a.r
                                                else "SIG" + a.s)),
          flush=True)
    g.buf = ""
    seen = ""
    if a.c:
        g.send("cd /root; %s; echo KI''LLED=$?" % a.c)
        # The trigger takes as long as it takes; liveness is asked after.
        try:
            seen += g.expect("KILLED=", a.T)
        except RuntimeError:
            seen += g.buf
            g.buf = ""
    elif a.r:
        g.send("minix-service refresh fs_c0d0; echo KI''LLED=$?")
    else:
        g.send("kill -%s %s; echo KI''LLED=$?" % (a.s, pid))
    # Liveness is an external program run from the root FS: the shell's
    # builtins answer without VFS, and `echo` alone once reported a hung
    # machine as healthy.
    time.sleep(2)
    t0 = time.time()
    g.send("/bin/ls / >/dev/null; /bin/echo FS''-OK=$?")
    verdict = "SILENT HANG"
    while time.time() - t0 < a.T:
        try:
            seen += g.expect("\n", 5)
        except RuntimeError:
            seen += g.buf
            g.buf = ""
            if g.p.poll() is not None:
                verdict = "QEMU exited"
                break
        if "FS-OK=" in seen:
            verdict = "root FS answers"
            break
        if re.search(r"panic|MEECHO will now|halted", seen):
            try:
                seen += g.expect("\n", 10)
            except RuntimeError:
                seen += g.buf
            verdict = "machine stopped"
            break
    print("after %.0f s: %s" % (time.time() - t0, verdict), flush=True)
    for line in seen.splitlines():
        if re.search(LOUD + "|FS-OK", line):
            print("    " + line.strip(), flush=True)
    if verdict == "root FS answers":
        # Does it still write, and does a second program start?
        g.buf = ""
        g.send("date > /root/after-restart; /bin/cat /root/after-restart; "
               "sync; echo WR''-END=$?")
        try:
            print(g.expect("WR-END=", 30).strip()[-300:])
        except RuntimeError:
            print("write after restart did not finish")
    if a.g and verdict == "SILENT HANG":
        gdb_dump()
    g.kill_group()
    return 0 if verdict != "SILENT HANG" else 1


if __name__ == "__main__":
    sys.exit(main())
