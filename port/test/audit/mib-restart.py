#!/usr/bin/env python3
"""Does sysctl(2) survive `minix-service refresh mib`?  (ISSUES.md, O54)

Every sysctl(2) is a sendrec to MIB_PROC_NR, and not only for ps and
sysctl(8): libc asks MIB while starting every program (the stack guard, the
page size for malloc).  The probe:

  1. starts LOOPS background loops, each doing ROUNDS pairs of
     `sysctl -n kern.ostype` and `sysctl -n hw.ncpu` - so new programs
     start, and call, all the time;
  2. while they run, refreshes mib REFRESH times, two seconds apart;
  3. waits for every loop to end; a loop whose round counter has not moved
     for two minutes is hung, and then every process's psinfo is printed;
  4. asks once more, in the foreground, and reads /proc/service/mib.

    python3 port/test/audit/mib-restart.py [-n REFRESH] [-l LOOPS]
                                            [-r ROUNDS] [-g KERNEL]

QEMU_SMP=4 for four cores.  -g boots a kernel built with -g and, on a hang,
prints through the gdbstub what the kernel holds for everyone who waits on
endpoint 7 (flags, getfrom/sendto, pc, mib's caller queue).  Log:
~/mib-restart.log.  Exit 0 only if every loop finished, no sysctl failed,
the last answer is MEECHO and mib counts as many restarts as RS accepted
refreshes (a refresh during a restart gets EBUSY, 16).  Markers are
split by quotes (EN''D) so that the terminal's echo of the typed line never
matches them.  No `ps` in the loops: one `ps -ax` is 19 s under TCG.

What it showed (2026-10-15, QEMU), one layer under another:
  before        every refresh left the callers mib had received and not
                answered in RECEIVING from 7 with MF_REPLY_PEND, forever:
                RS hands endpoint 7 to the new instance (RS_SWAP), so the
                kernel never sees 7 die.  On the board this was `sleep`,
                `cat`, `ps` in D "blocked on 7" (O54).
  mib_init      SYS_STATE_CLEAR_IPC_REFS on restart, as the drivers do:
                kernel panic, `p_q_link == NULL` in mini_send - clear_ipc()
                took a queued sender off the queue without unlinking it.
  clear_ipc     the woken callers spun in malloc's setup: sysconf(
                _SC_PAGESIZE) is a sysctl, and its failure is not a power
                of two.
  __sysctl      libc asks the new instance again on EDEADSRCDST.
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
# Its own work directory and a fresh disk: ~/obj-evbarm64/work belongs to
# whoever else runs QEMU on this machine.
os.environ.setdefault("WORK_DIR",
                      os.path.expanduser("~/obj-evbarm64/work-mibrestart"))
os.environ.setdefault("DISK_FRESH", "1")


GDBPORT = 1237
# With -g: the kernel's view of everyone who waits on mib, and mib's queue.
DUMP = r"""
python
import gdb
procs = gdb.parse_and_eval("proc")
n = int(procs.type.sizeof // procs[0].type.sizeof)
for i in range(n):
    p = procs[i]
    rts = int(p["p_rts_flags"])
    if rts & 1:
        continue
    ep = int(p["p_endpoint"])
    frm, to = int(p["p_getfrom_e"]), int(p["p_sendto_e"])
    if ep == 7 or frm == 7 or to == 7:
        q, qs = p["p_caller_q"], []
        while int(q) != 0:
            qs.append(int(q.dereference()["p_endpoint"]))
            q = q.dereference()["p_q_link"]
        print("KPROC %d %s ep=%d rts=%#x misc=%#x getfrom=%d sendto=%d pc=%#x lr=%#x q=%s"
              % (i, p["p_name"].string(), ep, rts, int(p["p_misc_flags"]),
                 frm, to, int(p["p_reg"]["pc"]), int(p["p_reg"]["lr"]), qs))
end
"""


def kdump(kern):
	path = "/tmp/mib-restart.gdb"
	with open(path, "w") as f:
		f.write(DUMP)
	out = subprocess.run(["gdb-multiarch", "-batch", "-nx",
	                      "-ex", "file " + kern,
	                      "-ex", "target remote :%d" % GDBPORT,
	                      "-x", path, "-ex", "detach"],
	                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
	                     timeout=120).stdout.decode(errors="replace")
	for line in out.splitlines():
		if line.startswith("KPROC"):
			print("    " + line, flush=True)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-n", type=int, default=5, help="refreshes")
	ap.add_argument("-l", type=int, default=3, help="background loops")
	ap.add_argument("-r", type=int, default=200, help="rounds per loop")
	ap.add_argument("-g", metavar="KERNEL",
	                help="kernel ELF with -g (port/kbuild-variant.sh dbg "
	                "\"-O2 -g\"): boot it and dump the kernel's side of "
	                "a hang through the gdbstub")
	a = ap.parse_args()
	if a.g:
		os.environ["KERNEL_ELF"] = a.g
		os.environ["QEMU_EXTRA"] = (os.environ.get("QEMU_EXTRA", "") +
		                            " -gdb tcp::%d" % GDBPORT).strip()

	log = open(os.path.expanduser("~/mib-restart.log"), "w")
	g = qt.Guest(log, timeout=1800)
	ok = True
	try:
		g.login()
		g.send("export PATH=$PATH:/sbin:/usr/sbin")
		g.expect("# ", 20)
		g.send("rm -f /tmp/mr.* /tmp/mp.*")
		g.expect("# ", 20)
		for i in range(a.l):
			g.send("(i=0; while [ $i -lt %d ]; do "
			       "sysctl -n kern.ostype >/dev/null || echo SF''AIL-%d; "
			       "sysctl -n hw.ncpu >/dev/null || echo PF''AIL-%d; "
			       "i=$((i+1)); echo $i > /tmp/mp.%d; done; "
			       "echo L''DONE-%d > /tmp/mr.%d) &"
			       % (a.r, i, i, i, i, i))
		# RS answers EBUSY (16) to a refresh that comes while the last
		# restart is still going on; only accepted ones count.
		refreshed = 0
		for n in range(a.n):
			time.sleep(2)
			g.buf = ""
			g.send("minix-service refresh mib; echo RF''=$?")
			try:
				out = g.expect("RF=", 60) + g.expect("\r", 5)
				m = re.search(r"RF=(\d+)", out)
				code = m.group(1) if m else "?"
			except RuntimeError:
				code = "none"
			print("refresh %d: %s" % (n + 1, code), flush=True)
			if code == "0":
				refreshed += 1
		g.buf = ""
		# Wait for the loops: their marker files.  A loop whose round
		# counter has not moved for STUCK seconds is hung, not slow.
		STUCK = 120
		done, last, moved = 0, None, time.time()
		while True:
			g.buf = ""
			g.send("echo N''=$(cat /tmp/mr.* 2>/dev/null | wc -l) "
			       "P''=$(cat /tmp/mp.* 2>/dev/null | tr '\\n' ,)E")
			try:
				out = g.expect("N=", 30) + g.expect("E\r", 10)
			except RuntimeError:
				break
			m = re.search(r"N=\s*(\d+) P=([\d,]*)E", out)
			if not m:
				continue
			done, prog = int(m.group(1)), m.group(2)
			if done == a.l:
				break
			if prog != last:
				last, moved = prog, time.time()
			elif time.time() - moved > STUCK:
				break
			time.sleep(10)
		print("loops finished: %d of %d (rounds %s of %d)"
		      % (done, a.l, last, a.r), flush=True)
		if done != a.l:
			ok = False
			# Who is stuck, and on whom: psinfo of every sysctl/ps.
			g.buf = ""
			g.send("for p in /proc/[0-9]*; do read l < $p/psinfo; "
			       "echo \"${p#/proc/} $l\"; done | "
			       "grep -v ' Z ' ; echo PS''-END")
			try:
				out = g.expect("PS-END", 60)
				for line in out.splitlines()[1:]:
					if "PS-END" not in line:
						print("    stuck: " + line.strip(), flush=True)
			except RuntimeError:
				pass
			if a.g:
				kdump(a.g)
		g.buf = ""
		g.send("sysctl -n kern.ostype; echo SY''=$?")
		try:
			out = g.expect("SY=", 60) + g.expect("\n", 5)
			body = out.split("\n", 1)[1] if "\n" in out else out
			good = "MEECHO" in body and "SY=0" in out
		except RuntimeError:
			good, out = False, g.buf
		print("sysctl afterwards: %s" % ("ok" if good else "FAILED"),
		      flush=True)
		ok = ok and good
		g.buf = ""
		g.send("grep restarts /proc/service/mib; echo PR''-END")
		try:
			out = g.expect("PR-END", 60)
			m = re.search(r"restarts:\s*(\d+)", out)
			restarts = int(m.group(1)) if m else -1
		except RuntimeError:
			restarts = -1
		print("mib restarts: %d (want %d)" % (restarts, refreshed),
		      flush=True)
		ok = ok and restarts == refreshed and refreshed > 0
		g.send("echo LO''G-END")
		whole = g.expect("LOG-END", 30)
	except RuntimeError as e:
		print("guest: %s" % e, flush=True)
		ok = False
		whole = g.buf
	finally:
		g.kill_group()
	log.close()
	text = open(os.path.expanduser("~/mib-restart.log"),
	            errors="replace").read()
	fails = len(re.findall(r"[SP]FAIL-\d", text))
	bad = re.findall(r".*(?:panic|died|RS:|MIB:|refresh).*", text)
	print("sysctl/ps failures inside loops: %d" % fails, flush=True)
	for line in bad[:20]:
		if "minix-service refresh" in line:
			continue
		print("    | " + line.strip(), flush=True)
	print("RESULT: %s" % ("ok" if ok else "FAILED"), flush=True)
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
