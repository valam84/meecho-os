#!/usr/bin/env python3
"""Время процесса на QEMU: ptprobe одной загрузкой (PORTING-LOG.md,
«Время процесса у событийных программ»).

Корень с диска в своём WORK_DIR (~/obj-evbarm64/work-ptime), зонд приезжает
в гостя по HTTP с хоста (10.0.2.2), а не через DESTDIR: файл в DESTDIR молча
попал бы в каждый следующий образ корня у всех. Зонд собирается заранее:

	aarch64-elf64-minix-gcc -O2 -static --sysroot=$DESTDIR \\
	    -o ~/obj-evbarm64/work-ptime/cc/ptprobe port/test/ptime/ptprobe.c

	qemu-ptime.py [-c ядер] [-2] [-3] [-t секунд] [-f] [-r прогоны] [-x]

-f — сделать образ диска заново (иначе тот, что уже лежит в WORK_DIR).
-x — ещё и потребители: TIME и %CPU работающего зонда в ps(1), строка
`[kernel]`, и time(1) - он идёт через times(2) к тем же двум полям.
-r — список прогонов через запятую, каждый «режим:работа_мкс»; по
умолчанию busy, timer:3000, timer:15000 и, если ядер больше одного,
pipe:3000 и pipe:15000 (погонщику нужно своё ядро).

Печатает строку ptprobe на каждый прогон и в конце — сверку: сколько тиков
начислено против сколько тиков насчитали бы cycles. Выход 0, если каждый
прогон начислил время с точностью до тика на всплеск (то, что должна давать
правка учёта); 1 — иначе. До правки ожидается 1.
"""

import argparse
import http.server
import os
import re
import select
import subprocess
import sys
import threading
import time

HOME = os.path.expanduser("~")
RAMIMAGE = os.environ.get("RAMIMAGE", os.path.join(HOME, "bin", "ramimage.sh"))
WORK = os.path.join(HOME, "obj-evbarm64", "work-ptime")
CC = os.path.join(WORK, "cc")
PORT = 8471


class Quiet(http.server.SimpleHTTPRequestHandler):
	def __init__(self, *a, **k):
		super().__init__(*a, directory=CC, **k)

	def log_message(self, *a):
		pass


def serve():
	srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Quiet)
	threading.Thread(target=srv.serve_forever, daemon=True).start()
	return srv


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", type=int, default=1)
	ap.add_argument("-2", dest="el2", action="store_true")
	ap.add_argument("-3", dest="gic3", action="store_true")
	ap.add_argument("-t", type=int, default=6)
	ap.add_argument("-f", action="store_true")
	ap.add_argument("-r", default="")
	# Also the consumers: ps(1) TIME of a running probe and of the kernel,
	# and time(1), which goes through times(2) - the same two fields.
	ap.add_argument("-x", action="store_true")
	a = ap.parse_args()

	runs = [r for r in a.r.split(",") if r] or (
	    ["busy:0", "timer:3000", "timer:15000"] +
	    (["pipe:3000", "pipe:15000"] if a.c > 1 else []))
	if not os.path.exists(os.path.join(CC, "ptprobe")):
		sys.exit("no %s/ptprobe: build it first" % CC)

	srv = serve()
	os.makedirs(WORK, exist_ok=True)
	env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="1800",
	           QEMU_SMP=str(a.c))
	if a.f:
		env["DISK_FRESH"] = "1"
	args = ["bash", RAMIMAGE, "-d"]
	if a.el2:
		args.append("-2")
	if a.gic3:
		args.append("-3")
	log = open(os.path.join(WORK, "qemu-ptime.log"), "w")
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
				return out
			if time.time() > deadline or p.poll() is not None:
				raise RuntimeError("no %r" % pat)
			if select.select([p.stdout], [], [], 0.2)[0]:
				c = p.stdout.read(1).decode(errors="replace")
				buf += c
				log.write(c)
				log.flush()

	def send(line):
		p.stdin.write((line + "\n").encode())
		p.stdin.flush()
		time.sleep(0.4)

	bad = 0
	try:
		expect(r"login:", 600)
		send("root")
		expect(r"# ", 60)
		send("ftp -o /tmp/ptprobe http://10.0.2.2:%d/ptprobe >/dev/null "
		     "2>&1; chmod 755 /tmp/ptprobe; ls -l /tmp/ptprobe; "
		     "echo FE''TCHED" % PORT)
		out = expect(r"FETCHED", 120)
		if "ptprobe" not in out.split("FETCHED")[0].split("ls -l")[-1]:
			raise RuntimeError("the probe did not arrive")
		print("QEMU: %d core(s)%s%s" % (a.c, ", EL2" if a.el2 else "",
		      ", GICv3" if a.gic3 else ""))
		for r in runs:
			mode, w = r.split(":")
			send("/tmp/ptprobe -m %s -w %s -t %d; echo PT''DONE"
			     % (mode, w, a.t))
			out = expect(r"PTDONE", 60 + 30 * a.t)
			lines = [l.strip() for l in out.splitlines()
			         if l.strip().startswith(("ptprobe ", "wake phase",
			                                  "on cpu"))]
			for l in lines:
				print("  " + l)
			m = re.search(r"hz=(\d+) .*wakes (\d+) .*cycles ([\d.]+) s "
			              r"charged [\d.]+ s \(user (\d+) sys (\d+)", out)
			if not m:
				print("  FAIL: no result")
				bad += 1
				continue
			hz, wakes, cyc = int(m.group(1)), int(m.group(2)), \
			    float(m.group(3))
			ut, st = int(m.group(4)) / hz, int(m.group(5)) / hz
			# User time against the process's own cycles.  System time
			# is the servers' and the kernel's work billed to it, which
			# cycles do not include, so it is printed, not compared.
			# Rounding to whole ticks loses at most one tick in total
			# when time comes from cycles; a sampling clock loses up to
			# one tick per burst - that is the difference looked for.
			ok = abs(cyc - ut) <= 2.0 / hz + 0.1 * wakes / hz
			print("  %s  user %.2f s of %.2f s run (sys %.2f s)" %
			      ("ok  " if ok else "FAIL", ut, cyc, st))
			bad += not ok
		if a.x:
			send("/tmp/ptprobe -m timer -w 3000 -t %d > /dev/null & "
			     "P=$!; sleep %d; ps -o pid,%%cpu,time,comm -p $P; "
			     "cat /proc/$P/psinfo; wait; ps axo pid,time,comm | "
			     "head -14; echo PS''DONE" % (a.t, a.t // 2))
			for l in expect(r"PSDONE", 120 + 30 * a.t).splitlines():
				if re.match(r"\s*(PID|\d+ |1 U)", l):
					print("  " + l.rstrip())
			send("time /tmp/ptprobe -m timer -w 3000 -t %d; echo TI''MED"
			     % a.t)
			for l in expect(r"TIMED", 120 + 30 * a.t).splitlines():
				if re.search(r"real|ptprobe timer", l):
					print("  " + l.strip())
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
	print("%d of %d runs off by more than a tick per tenth of the bursts"
	      % (bad, len(runs)))
	sys.exit(1 if bad else 0)


if __name__ == "__main__":
	main()
