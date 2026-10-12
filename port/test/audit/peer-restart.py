#!/usr/bin/env python3
"""Restart the socket drivers under load; VFS must survive it.

lwip and uds are started by /etc/rc through RS and can be restarted with
`minix-service refresh`. A restart is exactly the peer failure VFS has to
cope with: every process suspended on a socket of that driver gets its call
ended (sdev_stop), select()s on it are woken, and new sockets go to the new
instance. Before 2026-09-30 a restart that caught a process in the window
between a socket close(2) and its worker finishing could end in "process has
two calls".

Load: background ping loops through lwip, l20-udsload through uds. Then
each driver is restarted several times. Verdict: the shell still answers,
no panic on the console, and the load processes finish.

Usage (in WSL): python3 port/test/audit/peer-restart.py [-n rounds]
Needs the audit probes on the disk image (run.py builds them).
"""

import argparse
import importlib.util
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "qemu_tests", os.environ.get("QT", os.path.join(HERE, "..", "suite", "qemu-tests.py")))
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
qt.TESTDIR = "/usr/tests/audit"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", type=int, default=5, help="restarts per driver")
    a = ap.parse_args()

    log = open(os.path.expanduser("~/peer-restart.log"), "w")
    g = qt.Guest(log, timeout=3600)
    g.login()
    g.send("export PATH=$PATH:/sbin:/usr/sbin")
    g.expect("# ", 20)

    secs = 40 * a.n
    g.send("./l20-udsload %d > /tmp/uds.out 2>&1 &" % secs)
    g.send("for i in 1 2 3; do (end=$(($(date +%%s)+%d)); "
           "while [ $(date +%%s) -lt $end ]; do ping -c 1 10.0.2.2 "
           ">/dev/null 2>&1; done) & done" % secs)
    time.sleep(5)

    results = []
    for drv in ("uds", "lwip"):
        for i in range(a.n):
            g.buf = ""
            g.send("minix-service refresh %s; echo RS''-%s-%d=$?"
                   % (drv, drv, i))
            code = "?"
            try:
                out = g.expect("RS-%s-%d=" % (drv, i), 60)
                tail = g.expect("\n", 5)
                m = re.match(r"\s*(\d+)", tail)
                if m:
                    code = m.group(1)
            except RuntimeError:
                out = g.buf
            alive = g.alive()
            # A refresh that did not happen proves nothing: count it.
            bad = (re.search(r"panic|assert|two calls|MEECHO will now", out)
                   or code != "0")
            results.append((drv, i, alive and not bad))
            print("refresh %-4s %d: %s (exit %s)" % (drv, i,
                  "ok" if alive and not bad else "FAILED", code), flush=True)
            if not alive or bad:
                for line in out.splitlines():
                    if re.search(r"panic|assert|two calls", line):
                        print("    " + line.strip())
                g.kill_group()
                sys.exit(1)
            time.sleep(8)

    g.buf = ""
    g.send("wait; cat /tmp/uds.out; echo DO''NE")
    out = g.expect("DONE", secs + 120)
    m = re.search(r"udsload: .*", out)
    print(m.group(0) if m else "udsload printed nothing")
    ok = g.alive()
    g.close()
    print("system alive at the end: %s" % ok)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
