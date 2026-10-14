#!/usr/bin/env python3
"""PAN and the message copies, on QEMU.

Two questions, one boot:

  PAN     Does every core that has PAN enforce it after processes have run?
          The kernel answers itself (pan.c): the first interrupt each core
          takes from a process, it reads a byte of that process's stack with
          an ordinary load and expects a permission fault. The answer is a
          line per core - "PAN: on, cpu N" - or "PAN: FAILED", or "PAN: not
          implemented" on a core without it. A load on every core first, so
          that every core has run a process.

  KPTR    Does the kernel refuse a kernel address as a process's message
          buffer? kptr.c makes a kernel call with x0 pointing at
          k_overflow_stacks (from the kernel's symbol table). Refused: the
          program dies of SIGSEGV. Anything else - SURVIVED, or the
          kernel dispatching the call at all - is a failure.

    qemu-pan.py [-c cores] [-2] [-3] [-p cpu] [-n]
      -p  QEMU's -cpu, cortex-a55 by default (the board's core, with PAN);
          cortex-a72 has none
      -n  keep the disk image (kptr already on it)

The machine gets a work directory of its own (WORK_DIR, ~/work-pan by
default), so its disk image is nobody else's. KERNEL_ELF picks the kernel,
as for ramimage.sh. Log: ~/pan-qemu.log, or PAN_LOG. See
port/PORTING-LOG.md, "PAN".
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
NM = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-nm")
GUESTDIR = "/usr/tests/pan"

spec = importlib.util.spec_from_file_location(
    "qemu_tests", os.path.join(HERE, "..", "suite", "qemu-tests.py"))
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)
qt.TESTDIR = GUESTDIR


class Guest(qt.Guest):
	"""qemu-tests' guest, in a work directory of its own.

	No wait for other QEMUs: the one thing two of them cannot share is
	the disk image, and this one has its own.
	"""

	def __init__(self, log, flags, timeout=1200):
		env = dict(os.environ)
		env["QEMU_TIMEOUT"] = str(timeout)
		self.log = log
		self.p = subprocess.Popen(
		    ["bash", qt.RAMIMAGE, "-d"] + flags,
		    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		    stderr=subprocess.STDOUT, bufsize=0, env=env,
		    start_new_session=True)
		self.buf = ""

	def run(self, cmd, timeout):
		"""A command and its output, up to the end marker."""
		self.send("%s; echo EN''D=$?" % cmd)
		out = self.expect_re_end(timeout)
		m = re.search(r"END=(\d+)", out)
		return out, int(m.group(1)) if m else None

	def expect_re_end(self, timeout):
		acc = ""
		deadline = time.time() + timeout
		while not re.search(r"END=\d+", acc):
			left = deadline - time.time()
			if left <= 0:
				raise RuntimeError("no END in %d s" % timeout)
			acc += self.expect("\n", left)
		return acc


def build():
	out = os.path.join(DEST, GUESTDIR.lstrip("/"))
	os.makedirs(out, exist_ok=True)
	r = subprocess.call([XCC, "-O2", "-static", "-Wall",
	                     "--sysroot=" + DEST,
	                     "-o", os.path.join(out, "kptr"),
	                     os.path.join(HERE, "kptr.c")])
	if r != 0:
		sys.exit("build failed: kptr")


def kernel_symbol(name):
	kernel = os.environ.get("KERNEL_ELF",
	    os.path.join(HOME, "obj-evbarm64/minix/kernel/kernel"))
	text = subprocess.check_output([NM, kernel], text=True)
	for line in text.splitlines():
		f = line.split()
		if len(f) == 3 and f[2] == name:
			return int(f[0], 16)
	sys.exit("no %s in %s" % (name, kernel))


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", type=int, default=4, help="cores")
	ap.add_argument("-2", dest="el2", action="store_true", help="enter at EL2")
	ap.add_argument("-3", dest="gic3", action="store_true", help="GICv3")
	ap.add_argument("-p", default="cortex-a55", help="QEMU -cpu")
	ap.add_argument("-n", action="store_true", help="keep the disk image")
	a = ap.parse_args()

	build()
	# Mid-stack, well clear of either end: nothing but an overflowing core
	# ever touches it.
	target = kernel_symbol("k_overflow_stacks") + 4096

	os.environ.setdefault("WORK_DIR", os.path.join(HOME, "work-pan"))
	if not a.n:
		os.environ["DISK_FRESH"] = "1"
	os.environ["QEMU_SMP"] = str(a.c)
	os.environ["QEMU_CPU"] = a.p
	flags = (["-2"] if a.el2 else []) + (["-3"] if a.gic3 else [])

	log = open(os.environ.get("PAN_LOG",
	    os.path.join(HOME, "pan-qemu.log")), "w")
	g = Guest(log, flags)
	os.environ.pop("DISK_FRESH", None)
	boot = g.expect("login:", 600)
	g.send("root")
	g.expect("# ", 60)
	g.send("cd %s" % GUESTDIR)
	g.expect("# ", 60)

	# Every core runs a process for a while, so that every core has taken
	# an interrupt from one.
	load, _ = g.run("for i in 1 2 3 4 5 6 7 8; do awk 'BEGIN { while (i < "
	                "3000000) i++ }' & done; wait", 600)

	kout, kstatus = g.run("./kptr 0x%x" % target, 120)
	g.close()

	seen = boot + load + kout
	ok = True

	on = sorted(set(int(c) for c in
	                re.findall(r"PAN: on, cpu (\d+)", seen)))
	failed = re.findall(r"PAN: FAILED.*", seen)
	absent = "PAN: not implemented" in seen
	if absent:
		print("PAN      not implemented by %s" % a.p)
	else:
		print("PAN      verified on cpus %s of %d" % (on, a.c))
		if on != list(range(a.c)):
			ok = False
	for line in failed:
		print("         " + line.strip())
		ok = False

	survived = "SURVIVED" in kout
	read = re.findall(r"SYSTEM: .*request.*", kout)
	print("KPTR     exit %s, %s%s" % (kstatus,
	      "accepted (SURVIVED)" if survived else "refused",
	      ", kernel read it: " + read[0].strip() if read else ""))
	if survived or read or kstatus is None or kstatus == 0:
		ok = False

	down = re.findall(r".*(?:panic|kernel exception).*", seen)
	for line in down[:4]:
		print("DOWN     " + line.strip())
		ok = False

	print("RESULT   %s" % ("ok" if ok else "FAIL"))
	sys.exit(0 if ok else 1)


if __name__ == "__main__":
	main()
