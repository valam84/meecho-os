#!/usr/bin/env python3
"""Job control on QEMU: stop signals stop, SIGCONT continues.

Upstream MINIX's PM took the default action of every signal it did not
ignore to be termination, so "kill -STOP" ended the process (PORTING-LOG.md,
«Стоп-сигналы останавливают»).  What is checked:

  xray    The case that found it: "sleep 100 &", kill -STOP - the process
          is still there, its psinfo state is T; kill -CONT succeeds and the
          state is S again.
  frozen  A probe that only counts (jobprobe) is stopped while it computes:
          its count does not move for two seconds, the state is T; after
          kill -CONT it moves again, and the probe finishes its work and
          exits 0 - "wait" in the shell says so.
  test95  minix/tests/test95: waitpid(WUNTRACED/WCONTINUED), SIGCHLD and
          SA_NOCLDSTOP, signals that wait while stopped, SIGKILL and SIGTERM
          of a stopped process, SIGCONT caught, ignored, blocked, the
          discarding rules, SIGTSTP/SIGTTIN/SIGTTOU, the orphan rule.
  kill    kill -KILL of a stopped process ends it: status 137, /proc entry
          gone.
  init    kill -STOP 1 leaves init running.

Root from the disk in its own WORK_DIR (~/obj-evbarm64/work-jobctl); PM and
mib are those of ~/obj-jobctl (BOOT_IMAGES=), or with -S the shared ones.
The probe and test95 come to the guest over HTTP.  Build first:

	bash port/test/jobctl/build.sh

	qemu-jobctl.py [-c cores] [-2] [-3] [-S] [-f]
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
WORK = os.path.join(HOME, "obj-evbarm64", "work-jobctl")
CC = os.path.join(WORK, "cc")
VOBJ = os.path.join(HOME, "obj-jobctl")
PORT = 8473


class Quiet(http.server.SimpleHTTPRequestHandler):
	def __init__(self, *a, **k):
		super().__init__(*a, directory=CC, **k)

	def log_message(self, *a):
		pass


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", type=int, default=1)
	ap.add_argument("-2", dest="el2", action="store_true")
	ap.add_argument("-3", dest="gic3", action="store_true")
	ap.add_argument("-S", dest="shared", action="store_true")
	ap.add_argument("-f", action="store_true")
	ap.add_argument("-t", default="255",
	                help="test95 subtest masks, each run on its own: 1,2,4")
	a = ap.parse_args()

	for f in ("jobprobe", "test95"):
		if not os.path.exists(os.path.join(CC, f)):
			sys.exit("no %s/%s: port/test/jobctl/build.sh first" % (CC, f))

	srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Quiet)
	threading.Thread(target=srv.serve_forever, daemon=True).start()
	env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="1800",
	           QEMU_SMP=str(a.c))
	if not a.shared:
		imgs = []
		for name, rel in (("pm", "minix/servers/pm/pm"),
		                  ("mib", "minix/servers/mib/mib")):
			f = os.path.join(VOBJ, rel)
			if not os.path.exists(f):
				sys.exit("no %s: port/test/jobctl/build.sh first" % f)
			imgs.append("%s=%s" % (name, f))
		env["BOOT_IMAGES"] = " ".join(imgs)
	if a.f:
		env["DISK_FRESH"] = "1"
	args = ["bash", RAMIMAGE, "-d"] + (["-2"] if a.el2 else []) + \
	    (["-3"] if a.gic3 else [])
	log = open(os.path.join(WORK, "qemu-jobctl.log"), "w")
	p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
	                     stderr=subprocess.STDOUT, bufsize=0, env=env,
	                     start_new_session=True)
	buf = ""
	full = []		# everything the console said, for background output

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
				full.append(c)
				log.write(c)
				log.flush()

	def send(line):
		assert len(line) < 250, "the tty cuts lines at about 255"
		p.stdin.write((line + "\n").encode())
		p.stdin.flush()
		time.sleep(0.4)

	# A command's answer: the lines between two markers made by printf, so
	# that the echo of the typed line (which holds the marker's text only
	# split) cannot match.
	seq = [0]

	def ask(cmd, timeout=120):
		seq[0] += 1
		n = seq[0]
		send("printf 'Q%%sB\\n' %d; %s; printf 'Q%%sE\\n' %d" % (n, cmd, n))
		out, _ = expect(r"Q%dE\r?\n" % n, timeout)
		m = re.search(r"Q%dB\r?\n(.*)Q%dE" % (n, n), out, re.S)
		return m.group(1).replace("\r", "") if m else ""

	results = []

	def check(name, ok, detail):
		results.append(ok)
		print("  %s %-6s %s" % ("ok  " if ok else "FAIL", name, detail))

	def state(pidvar):
		return ask("cut -d' ' -f5 /proc/%s/psinfo 2>&1" % pidvar).strip()

	def count():
		s = ask("cat /tmp/cnt").strip()
		return int(s) if s.isdigit() else -1

	try:
		expect(r"login:", 900)
		send("root")
		expect(r"# ", 60)
		got = ask("for f in jobprobe test95; do ftp -o /tmp/$f "
		          "http://10.0.2.2:%d/$f >/dev/null 2>&1; done; chmod 755 "
		          "/tmp/jobprobe /tmp/test95; ls /tmp/jobprobe /tmp/test95"
		          % PORT)
		if "No such" in got or "/tmp/test95" not in got:
			raise RuntimeError("the probes did not arrive: %r" % got)
		print("QEMU: %d core(s)%s%s, PM and mib %s" % (a.c,
		      ", EL2" if a.el2 else "", ", GICv3" if a.gic3 else "",
		      "shared" if a.shared else "from " + VOBJ))
		# What a program started from the console shell gets for the job
		# control signals - information, not a check.
		for how, cmd in (("fg", "/tmp/jobprobe -s"),
		                 ("bg", "/tmp/jobprobe -s & wait")):
			print("  info   %s: %s" % (how, ask(cmd).strip()))

		# xray
		send("sleep 100 & P=$!")
		send("kill -STOP $P")
		time.sleep(1)
		s1 = state("$P")
		r = ask("kill -CONT $P && echo CONT-OK")
		time.sleep(1)
		s2 = state("$P")
		check("xray", s1 == "T" and "CONT-OK" in r and s2 == "S",
		      "after STOP %r, kill -CONT %r, after CONT %r" %
		      (s1, r.strip(), s2))
		ask("kill $P; wait $P")

		# frozen
		out = ask("/tmp/jobprobe -t 1")
		m = re.search(r"JOBPROBE rate (\d+)", out)
		if not m:
			raise RuntimeError("no rate: %r" % out)
		rate = int(m.group(1))
		n = rate * 8
		send("/tmp/jobprobe /tmp/cnt %d & P=$!" % n)
		time.sleep(2)
		c1 = count()
		ask("kill -STOP $P")
		# A probe stopped in the middle of sending its write is stopped
		# once the write is sent (DELAY_CALL), and the write may land
		# after kill returns: read the count a little later.
		time.sleep(0.5)
		c2 = count()
		time.sleep(2)
		c3 = count()
		s1 = state("$P")
		ask("kill -CONT $P")
		time.sleep(2)
		c4 = count()
		s2 = state("$P")
		check("frozen", 0 < c1 <= c2 == c3 < c4 and s1 == "T" and
		      s2 == "R", "count %d, stopped %d, 2 s later %d (%s), "
		      "continued 2 s later %d (%s)" % (c1, c2, c3, s1, c4, s2))
		out = ask("wait $P; echo WAIT $?", 120)
		done = ("JOBPROBE done %d" % n) in "".join(full)
		check("frozen", done and "WAIT 0" in out, "finished: %s%s" %
		      ("JOBPROBE done %d, " % n if done else "no JOBPROBE done, ",
		       " / ".join(l for l in out.splitlines() if l)))

		# test95, in the background so that a hang is seen and described
		for mask in a.t.split(","):
			ask("cd /tmp && (./test95 %s >/tmp/t95.out 2>&1; echo T95-EXIT "
			    "$? >>/tmp/t95.out) & cd /" % mask)
			deadline = time.time() + 240
			while time.time() < deadline:
				out = ask("cat /tmp/t95.out")
				if "T95-EXIT" in out:
					break
				time.sleep(5)
			else:
				# What its processes wait for: psinfo has the state and
				# the endpoint each is blocked on.
				out += ask("for f in /proc/[0-9]*/psinfo; do read v t e n "
				           "r < $f; [ $n = test95 ] && echo ${f%/psinfo} "
				           "$e $r; done")
				ask("for f in /proc/[0-9]*/psinfo; do read v t e n r < $f; "
				    "[ $n = test95 ] && p=${f%/psinfo} && kill -9 "
				    "${p#/proc/}; done")
			check("test95", re.search(r"Test 95 ok", out) is not None and
			      "T95-EXIT 0" in out, "mask %s: %s" % (mask, " / ".join(
			          l for l in out.splitlines() if l)[:600]))

		# kill
		send("sleep 100 & P=$!")
		send("kill -STOP $P")
		time.sleep(1)
		s1 = state("$P")
		out = ask("kill -KILL $P; wait $P; echo KILLED $?; "
		          "ls /proc/$P >/dev/null 2>&1 || echo GONE")
		check("kill", s1 == "T" and "KILLED 137" in out and "GONE" in out,
		      "stopped %r, then %s" % (s1, " / ".join(
		          l for l in out.splitlines() if l)))

		# init
		ask("kill -STOP 1")
		s1 = state("1")
		ask("kill -CONT 1")
		check("init", s1 not in ("T", ""), "init after kill -STOP: %r" % s1)

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
