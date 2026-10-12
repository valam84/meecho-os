#!/usr/bin/env python3
"""Restart boot-image services; which of them come back?

RS starts the boot-image services from the boot archive, so it has no path
to their binaries: a restart answers "read_exec failed: -2". etc/rc.minix
gives them one with `minix-service edit /service/<name> -label <name>`;
the aarch64 rc does not. This probe asks, one boot per service, what a
restart does once the path is there:

  1. minix-service edit /service/<svc> -label <svc>   (skipped with -E)
  2. minix-service refresh <svc>                       (SIGTERM, then RS
     restarts it from the binary)
  3. the shell still answers, and a check that needs that service passes.

    python3 port/test/audit/boot-restart.py [-E] [svc ...]

Default list: the boot-image services that are not the core (rs, vm, pm,
vfs are left out on purpose - see SERVER-AUDIT.md). Log: ~/boot-restart.log.
BOOTARGS="rootdevname=c0d0 console=tty00 rs_verbose=1" shows RS's side.

What it showed (2026-10-11, QEMU, disk root):
  without a path   every one: "read_exec failed: -2", "core system service
                   died", RS exits, kernel panic
  with a path      mib: ok (its restart callback is mib_init, a fresh
                   start). tty, memory, pfs, ds: the new instance fails
                   "initialization error: code 14" and the machine stops
                   the same way. sched: "unable to create service without
                   a replica" (SF_NEED_REPL).
tty, memory, pfs, ds, sched, vfs and pm all restart through
SEF_CB_INIT_RESTART_STATEFUL - a state transfer from the dead instance,
which libsys refuses for a crash that was not injected; see rs-crash.py.
So rc gives RS the path of mib only, and `-E mib` is the check for that.
"""

import argparse
import importlib.util
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "qemu_tests", os.environ.get("QT", os.path.join(HERE, "..", "suite", "qemu-tests.py")))
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
qt.TESTDIR = "/root"

# A command whose success needs the service, and the text it must print.
CHECK = {
    "tty":    ("echo tty-ok", "tty-ok"),
    "memory": ("dd if=/dev/zero bs=512 count=3 2>/dev/null | wc -c", "1536"),
    "mib":    ("sysctl -n kern.ostype", "MEECHO"),
    "pfs":    ("echo pfs-ok | cat", "pfs-ok"),
    "ds":     ("minix-service refresh uds && echo ds-ok", "ds-ok"),
    "sched":  ("awk 'BEGIN{for(i=0;i<200000;i++)s+=i; print \"sched-\" s}'",
               "sched-19999900000"),
}
BAD = r"panic|MEECHO will now|halted|died"


def run_one(svc, edit, log):
    g = qt.Guest(log, timeout=600)
    try:
        g.login()
        g.send("export PATH=$PATH:/sbin:/usr/sbin")
        g.expect("# ", 20)
        if edit:
            g.buf = ""
            g.send("minix-service edit /service/%s -label %s; echo ED''=$?"
                   % (svc, svc))
            g.expect("ED=", 60)
            ed = g.expect("\n", 5).strip()
        else:
            ed = "-"
        g.buf = ""
        g.send("minix-service refresh %s; echo RF''=$?" % svc)
        try:
            out = g.expect("RF=", 90)
            rf = g.expect("\n", 5).strip()
        except RuntimeError:
            out, rf = g.buf, "none"
        # Give RS a moment to bring it up before asking for it.
        g.send("sleep 3")
        if re.search(BAD, out):
            return ed, rf, "STOPPED", out
        if not g.alive():
            # refresh answers before the new instance initializes, so the
            # stop is often printed after RF= - look again.
            out += g.buf
            return ed, rf, "STOPPED" if re.search(BAD, out) else "HANG", out
        cmd, want = CHECK.get(svc, ("true", ""))
        g.buf = ""
        g.send("%s; echo CK''-END" % cmd)
        try:
            ck = g.expect("CK-END", 90)
        except RuntimeError:
            return ed, rf, "CHECK HANG", out + g.buf
        # The echo of the typed line contains `want` too for some checks;
        # look at what came after the command line.
        body = ck.split("\n", 1)[1] if "\n" in ck else ck
        ok = want in body
        return ed, rf, "ok" if ok else "CHECK FAILED", out + ck
    finally:
        g.kill_group()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-E", action="store_true", help="no edit first")
    ap.add_argument("svc", nargs="*",
                    default=["tty", "memory", "mib", "pfs", "ds", "sched"])
    a = ap.parse_args()
    log = open(os.path.expanduser("~/boot-restart.log"), "w")
    bad = 0
    for svc in a.svc:
        ed, rf, verdict, out = run_one(svc, not a.E, log)
        print("%-7s edit=%s refresh=%s -> %s" % (svc, ed, rf, verdict),
              flush=True)
        for line in out.splitlines():
            if re.search(r"initialization error|panic|died|read_exec|"
                         r"without a replica|unable", line):
                print("    | " + line.strip(), flush=True)
        if verdict != "ok":
            bad += 1
        qt.wait_no_qemu()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
