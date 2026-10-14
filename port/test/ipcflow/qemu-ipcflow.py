#!/usr/bin/env python3
"""The message matrix on QEMU: /proc/<pid>/ipc against /proc/ktrace.

What is checked (PORTING-LOG.md, «Рентген, часть первая»):

  exact   The probe (flowprobe) waits before and after its work - in
          open(2) of a FIFO, since MINIX's PM ends a process on SIGSTOP
          rather than stopping it - and while it waits, and its row has
          stopped changing, another process reads that row of the matrix
          and the "proc" rows of /proc/ktrace.  The two are counted
          in different places of the kernel - the matrix where a message
          leaves its sender (mini_send, mini_notify, a recorded SENDA, a
          kernel call), KTRACE where the process enters the kernel
          (do_ipc, kernel_call) - and for a program that only ever sends
          and receives its reply they must agree to the message:

              sum of "to" - kernel  ==  ipc + kcall           (the deltas)

          "kernel" is what the kernel sent in its name (page faults to VM,
          the end of a quantum to the scheduler), which no entry of its
          own asked for.
  who     The deltas go to the right services: at least N to PM
          (getpid), N to VFS (fstat), 2N to VM (mmap + munmap), and not
          wildly more.
  gone    A child sends K requests to VFS and exits: VFS's replies to it
          move to VFS's "gone" column (at least K), and no line of VFS's
          row names the child's endpoint any more.
  tasks   HARDWARE's row (/proc/-1/ipc) has interrupts in it.

The kernel is the KTRACE variant by default - port/kbuild-variant.sh
ktrace "-O2 -DKTRACE=1" - booted by KERNEL_ELF, so that the kernel the
shared object directory holds is not touched; -K shared boots that one
instead, and "exact" is then skipped (it has no /proc/ktrace).

Root from the disk in its own WORK_DIR (~/obj-evbarm64/work-ipcflow); the
probe and snap.sh come to the guest over HTTP, as port/test/ptime's do.
Build the probe first:

	aarch64-elf64-minix-gcc -O2 -static --sysroot=$DESTDIR \\
	    -o ~/obj-evbarm64/work-ipcflow/cc/flowprobe \\
	    port/test/ipcflow/flowprobe.c

	qemu-ipcflow.py [-c cores] [-2] [-3] [-n N] [-k K] [-K kernel] [-f]
"""

import argparse
import http.server
import os
import re
import select
import shutil
import subprocess
import sys
import threading
import time

HOME = os.path.expanduser("~")
SRC = os.environ.get("MINIXSRC", os.path.join(HOME, "minix-src"))
RAMIMAGE = os.environ.get("RAMIMAGE", os.path.join(HOME, "bin", "ramimage.sh"))
WORK = os.path.join(HOME, "obj-evbarm64", "work-ipcflow")
CC = os.path.join(WORK, "cc")
KTRACE_KERNEL = os.path.join(HOME, "obj-kvar", "ktrace", "minix", "kernel",
                             "kernel")
PORT = 8472


class Quiet(http.server.SimpleHTTPRequestHandler):
	def __init__(self, *a, **k):
		super().__init__(*a, directory=CC, **k)

	def log_message(self, *a):
		pass


def parse_snap(text, tag):
	"""The row (dict) and the KTRACE proc rows (slot -> (ipc, kcall))."""
	m = re.search(r"SNAP-BEGIN %s\r?\n(.*?)SNAP-END %s" % (tag, tag), text,
	              re.S)
	if not m:
		raise RuntimeError("no snapshot %s" % tag)
	row = {"to": {}, "kernel": None, "gone": None, "self": None}
	kt = {}
	for l in m.group(1).splitlines():
		w = l.split()
		if not w:
			continue
		if w[0] == "IPC" and len(w) >= 2:
			if w[1] == "to" and len(w) == 7:
				# to SLOT ENDPOINT PID NAME COUNT
				row["to"][int(w[2])] = (int(w[3]), int(w[4]), w[5],
				                        int(w[6]))
			elif w[1] in ("kernel", "gone", "tsc", "freq"):
				row[w[1]] = int(w[2])
			elif w[1] == "self":
				row["self"] = (int(w[2]), int(w[3]))
		elif w[0] == "KT" and len(w) == 6 and w[1] == "proc":
			kt[int(w[2])] = (int(w[4]), int(w[5]))
	return row, kt


def to_sum(row):
	return sum(v[3] for v in row["to"].values())


def by_name(row, name):
	return sum(v[3] for v in row["to"].values() if v[2] == name)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", type=int, default=1)
	ap.add_argument("-2", dest="el2", action="store_true")
	ap.add_argument("-3", dest="gic3", action="store_true")
	ap.add_argument("-n", type=int, default=1000)
	ap.add_argument("-k", type=int, default=200)
	ap.add_argument("-K", default=KTRACE_KERNEL)
	ap.add_argument("-f", action="store_true")
	a = ap.parse_args()

	os.makedirs(CC, exist_ok=True)
	shutil.copy(os.path.join(SRC, "port", "test", "ipcflow", "snap.sh"), CC)
	if not os.path.exists(os.path.join(CC, "flowprobe")):
		sys.exit("no %s/flowprobe: build it first" % CC)
	ktrace = a.K != "shared"

	srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Quiet)
	threading.Thread(target=srv.serve_forever, daemon=True).start()
	env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="1800",
	           QEMU_SMP=str(a.c))
	if ktrace:
		if not os.path.exists(a.K):
			sys.exit("no %s: port/kbuild-variant.sh ktrace "
			         "\"-O2 -DKTRACE=1\" first" % a.K)
		env["KERNEL_ELF"] = a.K
	if a.f:
		env["DISK_FRESH"] = "1"
	args = ["bash", RAMIMAGE, "-d"] + (["-2"] if a.el2 else []) + \
	    (["-3"] if a.gic3 else [])
	log = open(os.path.join(WORK, "qemu-ipcflow.log"), "w")
	p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
	                     stderr=subprocess.STDOUT, bufsize=0, env=env,
	                     start_new_session=True)
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
		assert len(line) < 250, "the tty cuts lines at about 255"
		p.stdin.write((line + "\n").encode())
		p.stdin.flush()
		time.sleep(0.4)

	results = []

	def check(name, ok, detail):
		results.append(ok)
		print("  %s %-6s %s" % ("ok  " if ok else "FAIL", name, detail))

	def snap(pid, tag, rowpid=None):
		send("sh /tmp/snap.sh %d %s %s" % (pid, tag,
		     rowpid if rowpid is not None else ""))
		out, _ = expect(r"SNAP-END %s" % tag, 120)
		return parse_snap(out, tag)

	try:
		expect(r"login:", 900)
		send("root")
		expect(r"# ", 60)
		send("for f in flowprobe snap.sh; do ftp -o /tmp/$f "
		     "http://10.0.2.2:%d/$f >/dev/null 2>&1; done; chmod 755 "
		     "/tmp/flowprobe; mkfifo /tmp/flowgo; ls /tmp/flowprobe /tmp/snap.sh; echo FE''TCHED"
		     % PORT)
		out, _ = expect(r"FETCHED", 120)
		if "/tmp/snap.sh" not in out or "No such" in out:
			raise RuntimeError("the probe did not arrive")
		print("QEMU: %d core(s)%s%s, kernel %s" % (a.c,
		      ", EL2" if a.el2 else "", ", GICv3" if a.gic3 else "",
		      "KTRACE variant" if ktrace else "shared"))

		# exact and who
		send("/tmp/flowprobe -g /tmp/flowgo -n %d &" % a.n)
		# Whole lines: the log is read a byte at a time, and "(\d+)" alone
		# matches the first digit of the pid.
		_, m = expect(r"FLOWPROBE wait a (\d+)\r?\n", 60)
		pid = int(m.group(1))
		ra, ka = snap(pid, "a")
		send("echo > /tmp/flowgo")
		expect(r"FLOWPROBE wait b %d\r?\n" % pid, 120 + a.n // 20)
		rb, kb = snap(pid, "b")
		send("echo > /tmp/flowgo")
		expect(r"FLOWPROBE done", 60)
		slot = rb["self"][0]
		dsum = to_sum(rb) - to_sum(ra)
		dkern = rb["kernel"] - ra["kernel"]
		print("  probe pid %d slot %d: %d messages, %d of them the "
		      "kernel's" % (pid, slot, dsum, dkern))
		for n in ("pm", "vfs", "vm", "mib", "system"):
			d = by_name(rb, n) - by_name(ra, n)
			if d:
				print("    to %-6s %d" % (n, d))
		if ktrace:
			if slot not in ka or slot not in kb:
				check("exact", False, "no KTRACE row for slot %d" % slot)
			else:
				dipc = kb[slot][0] - ka[slot][0]
				dkc = kb[slot][1] - ka[slot][1]
				check("exact", dsum - dkern == dipc + dkc,
				      "matrix %d - kernel %d = %d; ktrace ipc %d + kcall "
				      "%d = %d" % (dsum, dkern, dsum - dkern, dipc, dkc,
				                   dipc + dkc))
		n = a.n
		for name, lo in (("pm", n), ("vfs", n), ("vm", 2 * n)):
			d = by_name(rb, name) - by_name(ra, name)
			check("who", lo <= d <= lo + 50 + dkern,
			      "to %s %d (at least %d)" % (name, d, lo))
		vfs = [v for v in rb["to"].values() if v[2] == "vfs"]
		if not vfs:
			raise RuntimeError("the probe's row names no vfs")
		vfspid = vfs[0][1]

		# gone
		send("/tmp/flowprobe -g /tmp/flowgo -c %d &" % a.k)
		_, m = expect(r"FLOWPROBE wait a (\d+)\r?\n", 60)
		pid = int(m.group(1))
		va, _ = snap(pid, "ga", vfspid)
		send("echo > /tmp/flowgo")
		_, m = expect(r"FLOWPROBE child (\d+)\r?\n", 60)
		child = int(m.group(1))
		expect(r"FLOWPROBE wait b %d\r?\n" % pid, 120)
		vb, _ = snap(pid, "gb", vfspid)
		send("echo > /tmp/flowgo")
		expect(r"FLOWPROBE done", 60)
		dgone = vb["gone"] - va["gone"]
		named = [s for s, v in vb["to"].items() if v[1] == child]
		check("gone", dgone >= a.k and not named and
		      to_sum(vb) + vb["gone"] >= to_sum(va) + va["gone"],
		      "vfs (pid %d) gone +%d after child %d's %d requests%s" %
		      (vfspid, dgone, child, a.k,
		       "; still named in its row" if named else ""))

		# tasks
		send("cat /proc/-1/ipc; echo TA''SKS")
		out, _ = expect(r"TASKS", 60)
		irq = [l for l in out.splitlines() if l.startswith("to ")]
		check("tasks", len(irq) > 0 and all(int(l.split()[5]) > 0
		      for l in irq), "HARDWARE notifies %s" %
		      ", ".join("%s %s" % (l.split()[4], l.split()[5])
		                for l in irq))

		send("/sbin/poweroff")
		expect(r"halted|terminated|Power", 120)
	finally:
		try:
			p.stdin.close()
		except Exception:
			pass
		try:
			p.wait(timeout=60)
		except Exception:
			subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])
		srv.shutdown()
		log.close()
	bad = results.count(False)
	print("%d of %d checks failed" % (bad, len(results)))
	sys.exit(1 if bad or not results else 0)


if __name__ == "__main__":
	main()
