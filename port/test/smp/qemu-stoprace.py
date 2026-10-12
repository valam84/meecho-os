#!/usr/bin/env python3
"""stoprace on QEMU: does the kernel serve a call for a process PM stopped?

Builds stoprace.c into DESTDIR/usr/tests/smp, remakes the disk image so that
it carries it, boots with four cores (root on disk) and runs it for the
given number of rounds. A round is one of:

  RACE     the race showed: the kernel said it served a call for a stopped
           process (DEBUG_STOPRACE), PM panicked, or stoprace saw a wrong
           answer. The machine is logged into again if it went down.
  CLEAN    stoprace finished, said ok, and the kernel said nothing about
           calls served for stopped processes.
  TIMEOUT  neither, within the time allowed.

Kernel lines saying that an entry was undone are reported separately: they
are the fix at work, and they show that the window was hit at all. The
kernel prints the first sixteen and then every 1024th, each with the count
since boot, so the number shown is the highest count seen so far in this
boot - a lower bound, accurate to 1024.

    qemu-stoprace.py [-c cores] [-2] [-3] [-r rounds] [-t seconds]
                     [-a "stoprace args"] [-n]
      -n  keep the disk image (stoprace already on it)

Log: ~/stoprace-qemu.log. See port/PORTING-LOG.md, "Остановленный процесс
не входит в ядро".
"""

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HOME = os.path.expanduser("~")
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))
XCC = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-gcc")
GUESTDIR = "/usr/tests/smp"

spec = importlib.util.spec_from_file_location(
    "qemu_tests", os.path.join(HERE, "..", "suite", "qemu-tests.py"))
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
qt.TESTDIR = GUESTDIR

DOWN = r"panic|assert|MEECHO will now|kernel stack overflow"


class Guest(qt.Guest):
	"""qemu-tests' guest, with the entry level and the GIC version."""

	def __init__(self, log, flags, timeout=7200):
		qt.wait_no_qemu()
		env = dict(os.environ)
		env["QEMU_TIMEOUT"] = str(timeout)
		self.log = log
		self.p = subprocess.Popen(
		    ["bash", qt.RAMIMAGE, "-d"] + flags,
		    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		    stderr=subprocess.STDOUT, bufsize=0, env=env,
		    start_new_session=True)
		self.buf = ""

	def expect_re(self, pattern, timeout):
		"""Read until pattern matches; return everything up to the match.

		A round that brings the machine down says so in seconds, and
		waiting for its end marker instead costs the whole time limit.
		"""
		deadline = time.time() + timeout
		acc = ""
		while True:
			text = acc + self.buf
			m = re.search(pattern, text)
			if m:
				self.buf = text[m.end():]
				return text[:m.end()]
			left = deadline - time.time()
			if left <= 0:
				self.buf = text
				raise RuntimeError("no %r in %.0f s" % (pattern, timeout))
			try:
				# Whole lines, so that a match is never split.
				acc += self.expect("\n", min(left, 5))
			except RuntimeError:
				pass


def build():
	out = os.path.join(DEST, GUESTDIR.lstrip("/"))
	os.makedirs(out, exist_ok=True)
	r = subprocess.call([XCC, "-O2", "-static", "-Wall",
	                     "--sysroot=" + DEST,
	                     "-o", os.path.join(out, "stoprace"),
	                     os.path.join(HERE, "stoprace.c")])
	if r != 0:
		sys.exit("build failed: stoprace")


def one_round(g, args, secs):
	g.buf = ""
	g.send("./stoprace %s; echo EN''D=$?" % args)
	t0 = time.time()
	try:
		out = g.expect_re(r"END=\d+|MEECHO will now reset", secs + 300)
	except RuntimeError:
		out = g.buf
	elapsed = time.time() - t0
	served = re.findall(r"KERNEL: stoprace \d+: .*", out)
	undone = re.findall(r"KERNEL: stoprace: .* undone .*", out)
	result = re.findall(r"stoprace: \d+ victims.*", out)
	down = re.search(DOWN, out)
	corrupt = "stoprace: CORRUPT" in out
	if down or served or corrupt:
		verdict = "RACE"
	elif "stoprace: ok" in out:
		verdict = "CLEAN"
	else:
		verdict = "TIMEOUT"
	if down or not g.alive():
		try:
			out += g.expect("login:", 300)
			g.buf = "login:"
			g.login_at_prompt()
			g.send("cd %s" % GUESTDIR)
			g.expect("# ", 60)
		except RuntimeError:
			g.kill_group()
			raise
	return verdict, served, undone, result, out, elapsed


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", type=int, default=4, help="cores")
	ap.add_argument("-2", dest="el2", action="store_true", help="enter at EL2")
	ap.add_argument("-3", dest="gic3", action="store_true", help="GICv3")
	ap.add_argument("-r", type=int, default=1, help="rounds")
	ap.add_argument("-t", type=int, default=120, help="seconds per round")
	ap.add_argument("-a", default="", help="more stoprace arguments")
	ap.add_argument("-n", action="store_true", help="keep the disk image")
	a = ap.parse_args()

	build()
	if not a.n:
		os.environ["DISK_FRESH"] = "1"
	os.environ["QEMU_SMP"] = str(a.c)
	flags = (["-2"] if a.el2 else []) + (["-3"] if a.gic3 else [])

	log = open(os.path.join(HOME, "stoprace-qemu.log"), "w")
	g = Guest(log, flags)
	os.environ.pop("DISK_FRESH", None)
	g.login()
	args = ("-t %d %s" % (a.t, a.a)).strip()
	races = 0
	undone_since_boot = 0
	for i in range(a.r):
		verdict, served, undone, result, out, elapsed = \
		    one_round(g, args, a.t)
		races += verdict == "RACE"
		for line in undone:
			m = re.search(r"(\d+) so far", line)
			if m:
				undone_since_boot = max(undone_since_boot,
				                        int(m.group(1)))
		if re.search(DOWN, out):
			undone_since_boot = 0	# the counter went with the boot
		print("round %d: %-7s after %.1f s: served %d, undone since "
		      "boot >= %d" % (i + 1, verdict, elapsed, len(served),
		      undone_since_boot), flush=True)
		for line in result + served[:4] + undone[:1]:
			print("    " + line.strip())
		for line in out.splitlines():
			if re.search(DOWN, line):
				print("    " + line.strip())
	g.close()
	print("\n%d rounds, %d showed the race" % (a.r, races))


if __name__ == "__main__":
	main()
