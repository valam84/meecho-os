#!/usr/bin/env python3
"""What a kernel panic says about the kernel's own stack, on QEMU.

Builds a kernel with PANICPROBE (minix/kernel/debug.h) in an object
directory of its own (port/kbuild-variant.sh, variant "kstack"), boots it
with panicprobe=MODE, and waits for the panic it causes twenty seconds in:

  1  panic() called three functions deep - probe_a, probe_b, probe_c
  2  a load from an unmapped kernel address in probe_c: a kernel exception,
     then the panic from inkernel_disaster()

The "kernel on CPU n:" line is put through port/kstack.sh against the same
kernel, and the trace has to name the three probe functions and
trap_handler, where the chain starts - in mode 2 on the far side of the
exception, which only the frame record exception_entry_kernel builds makes
possible.

    qemu-kstack.py [-m 1|2|both] [-c cores] [-2] [-3] [-k]
      -k  use the kernel already built (no rebuild)

Log: ~/kstack-qemu.log. Work directory: ~/work-kstack. See
port/PORTING-LOG.md, "Стек ядра в панике".
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
PORT = os.path.normpath(os.path.join(HERE, "..", ".."))
HOME = os.path.expanduser("~")
RAMIMAGE = os.environ.get("RAMIMAGE", os.path.join(HOME, "bin", "ramimage.sh"))
VARIANT = "kstack"
KERNEL = os.path.join(HOME, "obj-kvar", VARIANT, "minix/kernel/kernel")
WORK_DIR = os.path.join(HOME, "work-kstack")

WANT = {
	1: ["probe_c", "probe_b", "probe_a", "panic_probe", "trap_handler"],
	2: ["inkernel_disaster", "exception_entry_kernel", "probe_c",
	    "probe_b", "probe_a", "panic_probe", "trap_handler"],
}


def build():
	r = subprocess.call(["bash", os.path.join(PORT, "kbuild-variant.sh"),
	                     VARIANT, "-O2 -DPANICPROBE=1"])
	if r != 0:
		sys.exit("kernel build failed")


def boot(mode, cores, flags, log):
	env = dict(os.environ)
	env.update(KERNEL_ELF=KERNEL, QEMU_SMP=str(cores), QEMU_TIMEOUT="300",
	           WORK_DIR=WORK_DIR,
	           BOOTARGS="rootdevname=c0d0 console=tty00 panicprobe=%d"
	           % mode)
	p = subprocess.Popen(["bash", RAMIMAGE, "-d"] + flags,
	                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
	                     stderr=subprocess.STDOUT, env=env,
	                     start_new_session=True)
	out = ""
	deadline = time.time() + 280
	tail = None
	while time.time() < deadline:
		r, _, _ = select.select([p.stdout], [], [], 1)
		if r:
			chunk = os.read(p.stdout.fileno(), 4096)
			if not chunk:
				break
			text = chunk.decode(errors="replace")
			out += text
			log.write(text)
			log.flush()
		if tail is None and re.search(r"kernel on CPU \d+:.*\n", out):
			tail = time.time() + 3	# the lines after it
		if tail is not None and time.time() > tail:
			break
	try:
		os.killpg(p.pid, signal.SIGKILL)
	except OSError:
		pass
	p.wait()
	# timeout(1) puts QEMU in a process group of its own, out of reach of
	# the kill above; it is found by its work directory, which is this
	# stand's alone, and the next boot needs its disk image released.
	for _ in range(30):
		if subprocess.call(["pkill", "-9", "-f", WORK_DIR]) != 0:
			break
		time.sleep(1)
	return out


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-m", default="both")
	ap.add_argument("-c", type=int, default=1, help="cores")
	ap.add_argument("-2", dest="el2", action="store_true")
	ap.add_argument("-3", dest="gic3", action="store_true")
	ap.add_argument("-k", action="store_true", help="no rebuild")
	a = ap.parse_args()

	if not a.k:
		build()
	modes = [1, 2] if a.m == "both" else [int(a.m)]
	flags = (["-2"] if a.el2 else []) + (["-3"] if a.gic3 else [])
	log = open(os.path.join(HOME, "kstack-qemu.log"), "w")
	ok = True
	first = True
	for mode in modes:
		if first:
			os.environ["DISK_FRESH"] = "1"
			first = False
		else:
			os.environ.pop("DISK_FRESH", None)
		out = boot(mode, a.c, flags, log)
		start = out.find("panic probe:")
		if start < 0:
			print("mode %d: no panic probe in the log" % mode)
			ok = False
			continue
		report = out[start:]
		named = subprocess.run(["bash", os.path.join(PORT, "kstack.sh"),
		                        KERNEL], input=report, text=True,
		                       capture_output=True).stdout
		print("mode %d:" % mode)
		for line in report.splitlines():
			if re.search(r"panic|kernel exception|^pc |pagefault", line):
				print("  " + line.strip())
		print(named.rstrip())
		trace = named.split("kernel on CPU", 1)[-1]
		missing = [f for f in WANT[mode] if not re.search(
		    r"\b%s\b" % f, trace)]
		print("  %s" % ("all of " + ", ".join(WANT[mode]) if not missing
		      else "MISSING: " + ", ".join(missing)))
		ok = ok and not missing
	print("RESULT   %s" % ("ok" if ok else "FAIL"))
	sys.exit(0 if ok else 1)


if __name__ == "__main__":
	main()
