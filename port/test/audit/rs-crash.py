#!/usr/bin/env python3
"""Crash RS itself; the machine must say so and stop, not hang.

RS is the signal manager of every system service and its own: a lethal
signal to RS goes to RS's backup signal manager or, without one, ends in
the kernel's "cause_sig: sig manager 2 gets lethal signal ... for itself".
The backup is RS's replica (clone_service() names it), and nothing on this
port made one: `minix-service clone rs` was never called.

How to crash it: PM refuses signals to system processes even from root, and
RS has no "crash" request (nor should it). So the probe goes through QEMU's
gdbstub: it finds the process with endpoint RS_PROC_NR in the kernel process
table and sets its saved pc to an unmapped address. The next time RS is
scheduled it takes an instruction abort, and the kernel raises SIGSEGV
through cause_sig() - the same path a real fault in RS would take.

Needs a kernel with types for gdb: build it with DBG="-O2 -g" (codegen is the
same, kernel.bin carries no DWARF).

    python3 port/test/audit/rs-crash.py [-c] [-n rounds] [-T s]

  -c  `minix-service clone rs` before the first crash (on a system whose rc
      already does it, this answers EEXIST - fine)
  -n  how many times to crash RS (each must be survived; the replica has to
      come back between rounds)

  -e  `minix-service -c edit /service/rs` first: the boot-image RS has no
      binary path, and clone answers "read_exec failed: -2" without it

What it showed (2026-10-10, QEMU):
  no replica      kernel panic "cause_sig: sig manager 2 gets lethal signal
                  11 for itself"
  -e -c           the replica takes the signal, restarts RS, and the new
                  instance refuses the old one's state: "SEF_CB_INIT_RESTART_
                  STATEFUL failed: -14", then "RS unable to complete init"
Recovering RS from a crash needs a state transfer from the instance that
crashed, and libsys refuses one by design unless the crash was injected
("crash was not controlled, aborting transparent restart"); upstream did it
only in LLVM-instrumented builds (ltckpt). So the honest outcome is the one
the root FS has: a loud stop. The probe checks exactly that.

Verdict: "loud stop" (the console says so and the machine stops) and
"survived" pass; "HANG" - silence - fails. Log: ~/rs-crash.log.
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

KERN = os.path.expanduser("~/obj-evbarm64/minix/kernel/kernel")
BAD = r"panic|MEECHO will now|cause_sig|halted"

INJECT = r"""
python
import gdb
procs = gdb.parse_and_eval("proc")
n = int(procs.type.sizeof // procs[0].type.sizeof)
done = False
for i in range(n):
    p = procs[i]
    if int(p["p_rts_flags"]) & 1:
        continue
    print("PROC %d %s ep=%d rts=%#x" % (i, p["p_name"].string(),
          int(p["p_endpoint"]), int(p["p_rts_flags"])))
    if int(p["p_endpoint"]) == 2 and not done:
        a = int(p["p_reg"]["pc"].address)
        old = int(p["p_reg"]["pc"])
        gdb.execute("set var *(unsigned long *)%#x = 8" % a)
        print("RSPC slot %d pc %#x -> 0x8" % (i, old))
        done = True
end
"""


def inject():
    path = "/tmp/rs-crash.gdb"
    with open(path, "w") as f:
        f.write(INJECT)
    out = subprocess.run(["gdb-multiarch", "-batch", "-nx",
                          "-ex", "file " + KERN,
                          "-ex", "target remote :1234",
                          "-x", path, "-ex", "detach"],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=120).stdout.decode(errors="replace")
    for line in out.splitlines():
        if line.startswith(("PROC", "RSPC")) and (" rs " in line or
                                                  line.startswith("RSPC")):
            print("  " + line, flush=True)
    return "RSPC" in out


def rs_answers(g, tag):
    """Ask RS something that needs RS: clone rs. 0 = made a replica,
    EEXIST = one is already there; both mean RS is alive."""
    g.buf = ""
    g.send("minix-service clone rs; echo CL''-%s=$?" % tag)
    try:
        out = g.expect("CL-%s=" % tag, 60)
        tail = g.expect("\n", 5)
    except RuntimeError:
        return None, g.buf
    m = re.match(r"\s*(\d+)", tail)
    return (m.group(1) if m else "?"), out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-c", action="store_true", help="clone rs first")
    ap.add_argument("-e", action="store_true",
                    help="minix-service -c edit /service/rs first: the boot "
                    "image RS has no binary path, so clone cannot read one")
    ap.add_argument("-n", type=int, default=1, help="crashes")
    ap.add_argument("-T", type=int, default=30, help="seconds after a crash")
    a = ap.parse_args()

    log = open(os.path.expanduser("~/rs-crash.log"), "w")
    os.environ["QEMU_EXTRA"] = (os.environ.get("QEMU_EXTRA", "") +
                                " -gdb tcp::1234").strip()
    g = qt.Guest(log, timeout=1800)
    g.login()
    g.send("export PATH=$PATH:/sbin:/usr/sbin")
    g.expect("# ", 20)

    if a.e:
        g.buf = ""
        g.send("minix-service -c edit /service/rs -label rs; echo ED''=$?")
        g.expect("ED=", 60)
        print("edit rs: exit %s" % g.expect("\n", 5).strip(), flush=True)
    if a.c:
        code, out = rs_answers(g, "pre")
        print("clone rs: exit %s" % code, flush=True)

    verdict = "survived"
    for r in range(a.n):
        time.sleep(3)
        if not inject():
            print("round %d: RS not found in the process table" % r)
            verdict = "NO RS"
            break
        g.buf = ""
        seen = ""
        t0 = time.time()
        # RS wakes on its own alarm; give the restart time to happen.
        while time.time() - t0 < a.T:
            try:
                seen += g.expect("\n", 1)
            except RuntimeError:
                seen += g.buf
                g.buf = ""
            if re.search(BAD, seen):
                break
        for line in seen.splitlines():
            if line.strip():
                print("  | " + line.rstrip(), flush=True)
        if re.search(BAD, seen):
            verdict = "loud stop"
            break
        if not g.alive():
            verdict = "HANG (shell)"
            break
        code, out = rs_answers(g, str(r))
        print("round %d: shell answers, clone rs exit %s" % (r, code),
              flush=True)
        if code is None:
            verdict = "HANG (RS does not answer)"
            break

    print("verdict: %s" % verdict, flush=True)
    g.kill_group()
    return 0 if verdict in ("survived", "loud stop") else 1


if __name__ == "__main__":
    sys.exit(main())
