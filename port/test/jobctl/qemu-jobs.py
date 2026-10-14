#!/usr/bin/env python3
"""Interactive job control on QEMU: ash with JOBS, on the console and on a pty.

PM has process groups apart from sessions, tty and pty a foreground process
group and SUSP, ash is built with JOBS (PORTING-LOG.md, «Управление
заданиями: группы, терминал, ash»).  This types into a real login shell what
a person would, and checks what comes back:

  monitor  the login shell has job control on: "m" in $-
  susp     a foreground job, ^Z: ash says it is suspended; jobs lists it;
           its state is T
  bg       bg: it runs again (state S), and jobs says Running
  fg       fg: the shell waits for it again; ^C ends it, $? is 130, and the
           shell is still there
  intr     ^C ends the foreground job and not a background one
  ttin     a background job that reads the terminal is stopped with
           SIGTTIN ("Stopped (tty input)"); fg, and it reads a typed line
  inread   a job stopped by ^Z inside its read: the shell reads its next
           command all the same, and after fg the job reads its line
  pipe     ^Z stops a whole pipeline; kill %1 ends it

The same over ssh -tt (a pty, /dev/pts), and then test95, test96 and the
native tests 5 and 37 (run -t), in the guest.

Two boots of the disk root in its own WORK_DIR (~/obj-evbarm64/work-jobs).
The first fetches ash, pty, libc.so and the tests over HTTP and puts them in
place (the disk persists); the second is the run.  PM, mib and tty are those
of ~/obj-jobs (BOOT_IMAGES=), or with -S the shared ones.  Build first:

	bash port/test/jobctl/build-jobs.sh

	qemu-jobs.py [-c cores] [-2] [-3] [-S] [-f] [--no-ssh] [--no-tests]
	             [-w N] [--gdb PORT]

-w N gives the run a WORK_DIR and ports of its own (work-jobs-N), so that
several combinations go at once; --gdb opens QEMU's gdbstub, to look at a
hang with port/test/gdb/pdump.py (boot a kernel with DWARF: KERNEL_ELF= a
port/kbuild-variant.sh build with "-O2 -g").

Log: ~/obj-evbarm64/work-jobs/qemu-jobs.log (console), qemu-jobs-ssh.log.
"""

import argparse
import glob
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
WORK = os.path.join(HOME, "obj-evbarm64", "work-jobs")
CC = os.path.join(WORK, "cc")
VOBJ = os.path.join(HOME, "obj-jobs")
KEY = os.path.join(HOME, ".ssh/qemu_meecho")
PORT = 8474
SSHPORT = 2232


class Quiet(http.server.SimpleHTTPRequestHandler):
	def __init__(self, *a, **k):
		super().__init__(*a, directory=CC, **k)

	def log_message(self, *a):
		pass


class Term:
	"""A terminal to type into and read from: the console, or ssh -tt."""

	def __init__(self, proc, log):
		self.p = proc
		self.log = log
		self.buf = ""
		self.seq = 0

	def pump(self, timeout):
		if select.select([self.p.stdout], [], [], timeout)[0]:
			c = os.read(self.p.stdout.fileno(), 4096)
			if not c:
				raise RuntimeError("the terminal closed")
			c = c.decode(errors="replace")
			self.buf += c
			self.log.write(c)
			self.log.flush()

	def expect(self, pat, timeout):
		deadline = time.time() + timeout
		while True:
			m = re.search(pat, self.buf)
			if m:
				out = self.buf[:m.end()]
				self.buf = self.buf[m.end():]
				return out
			if time.time() > deadline or self.p.poll() is not None:
				raise RuntimeError("no %r" % pat)
			self.pump(0.2)

	def drain(self, secs):
		"""Read what comes for a while; return it, and forget it."""
		deadline = time.time() + secs
		while time.time() < deadline:
			self.pump(0.1)
		out, self.buf = self.buf, ""
		return out

	def key(self, s):
		self.p.stdin.write(s.encode())
		self.p.stdin.flush()

	def send(self, line):
		assert len(line) < 250, "the tty cuts lines at about 255"
		self.key(line + "\n")
		time.sleep(0.4)

	def ask(self, cmd, timeout=120, pre=""):
		"""A command's answer: the lines between two markers made by
		printf, which the echo of the typed line cannot match.  'pre'
		goes before the first marker: "r=$?" keeps the status of what
		ran before the line, which the marker's printf would replace."""
		self.seq += 1
		n = self.seq
		self.send("%sprintf 'Q%%sB\\n' %d; %s; printf 'Q%%sE\\n' %d" %
		          (pre + "; " if pre else "", n, cmd, n))
		out = self.expect(r"Q%dE\r?\n" % n, timeout)
		m = re.search(r"Q%dB\r?\n(.*)Q%dE" % (n, n), out, re.S)
		return m.group(1).replace("\r", "") if m else ""


results = []


def check(where, name, ok, detail):
	results.append(ok)
	print("  %s %-5s %-8s %s" % ("ok  " if ok else "FAIL", where, name,
	                              detail[:300]))
	sys.stdout.flush()


def oneline(s):
	return " / ".join(l.strip() for l in s.replace("\r", "").splitlines()
	                  if l.strip())


def jobs(t, where):
	"""The job control checks, on one terminal."""

	def state(pid):
		return t.ask("cut -d' ' -f5 /proc/%s/psinfo 2>&1" % pid).strip()

	def await_state(pid, want, secs=5):
		s = ""
		for _ in range(secs * 2):
			s = state(pid)
			if s == want:
				break
			time.sleep(0.5)
		return s

	def prompt():
		return t.ask("echo ALIVE").strip() == "ALIVE"

	out = t.ask("echo $-")
	check(where, "monitor", "m" in out.strip(), "$- is %r" % out.strip())

	# susp, bg, fg
	t.send("sleep 100")
	time.sleep(1.5)
	t.key("\x1a")
	try:
		out = t.expect(r"Suspended", 30)
		ok = True
	except RuntimeError:
		out, ok = t.drain(1), False
	t.drain(1)
	jl = t.ask("jobs -l")
	m = re.search(r"\[1\]\s*[+-]?\s*(\d+)\s+Suspended\s+sleep 100", jl)
	pid = m.group(1) if m else None
	s1 = await_state(pid, "T") if pid else "?"
	check(where, "susp", ok and pid is not None and s1 == "T",
	      "^Z: %s; jobs -l: %s; state %s" % (oneline(out)[-60:],
	                                         oneline(jl), s1))
	if pid is None:
		t.ask("kill -9 %1 2>/dev/null; wait")
		return
	out = t.ask("bg")
	s2 = await_state(pid, "S")
	jl = t.ask("jobs")
	check(where, "bg", "sleep 100" in out and s2 == "S" and
	      "Running" in jl, "bg: %s; state %s; jobs: %s" % (
	          oneline(out), s2, oneline(jl)))
	t.send("fg")
	time.sleep(1.5)
	out = t.drain(0.5)
	t.key("\x03")
	time.sleep(1)
	rc = t.ask("echo RC $r", pre="r=$?")
	gone = t.ask("kill -0 %s 2>/dev/null || echo GONE" % pid)
	jl = t.ask("jobs")
	check(where, "fg", "sleep 100" in out and "RC 130" in rc and
	      "GONE" in gone and jl.strip() == "" and prompt(),
	      "fg: %s; after ^C: %s, %s, jobs %r" % (oneline(out), oneline(rc),
	                                            oneline(gone), jl.strip()))

	# intr: ^C reaches the foreground only
	t.send("sleep 300 &")
	bgpid = t.ask("echo BG $!").strip().replace("BG ", "")
	t.send("sleep 100")
	time.sleep(1.5)
	t.key("\x03")
	time.sleep(1)
	rc = t.ask("echo RC $r", pre="r=$?")
	s = state(bgpid)
	jl = t.ask("jobs")
	check(where, "intr", "RC 130" in rc and s == "S" and "Running" in jl
	      and prompt(), "after ^C: %s; background %s is %r; jobs: %s" % (
	          oneline(rc), bgpid, s, oneline(jl)))
	t.ask("kill %1; wait")

	# ttin: a background reader stops; fg, and it reads
	t.send("cat &")
	cpid = t.ask("echo CAT $!").strip().replace("CAT ", "")
	time.sleep(2)
	s = await_state(cpid, "T")
	jl = t.ask("jobs")
	t.send("fg")
	time.sleep(1.5)
	t.send("hello jobs")
	time.sleep(1)
	t.key("\x04")
	time.sleep(1)
	out = t.drain(0.5)
	rc = t.ask("echo RC $r", pre="r=$?")
	check(where, "ttin", s == "T" and "Stopped (tty input)" in jl and
	      out.count("hello jobs") >= 2 and "RC 0" in rc,
	      "state %r; jobs: %s; after fg: %s; %s" % (
	          s, oneline(jl), oneline(out)[-80:], oneline(rc)))

	# inread: a job stopped inside its read lets the shell read
	t.send("cat")
	time.sleep(1.5)
	t.key("\x1a")
	try:
		out = t.expect(r"Suspended", 30)
		ok = True
	except RuntimeError:
		out, ok = t.drain(1), False
	t.drain(1)
	alive = prompt()
	t.send("fg")
	time.sleep(1.5)
	t.send("read again")
	time.sleep(1)
	t.key("\x04")
	time.sleep(1)
	out2 = t.drain(0.5)
	rc = t.ask("echo RC $r", pre="r=$?")
	check(where, "inread", ok and alive and out2.count("read again") >= 2
	      and "RC 0" in rc, "^Z: %s; shell read %s; after fg: %s; %s" % (
	          oneline(out)[-40:], alive, oneline(out2)[-80:], oneline(rc)))

	# pipe: ^Z stops the whole pipeline
	t.send("sleep 100 | sleep 101")
	time.sleep(1.5)
	t.key("\x1a")
	try:
		t.expect(r"Suspended", 30)
		ok = True
	except RuntimeError:
		ok = False
	t.drain(1)
	jl = t.ask("jobs -l")
	pids = re.findall(r"(\d+)\s+(?:Suspended\s+)?sleep 10[01]", jl)
	sts = [await_state(p, "T") for p in pids]
	t.ask("kill %1; wait")
	check(where, "pipe", ok and len(pids) == 2 and sts == ["T", "T"] and
	      t.ask("jobs").strip() == "", "jobs -l: %s; states %s" % (
	          oneline(jl), sts))


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", type=int, default=1)
	ap.add_argument("-2", dest="el2", action="store_true")
	ap.add_argument("-3", dest="gic3", action="store_true")
	ap.add_argument("-S", dest="shared", action="store_true")
	ap.add_argument("-f", action="store_true")
	ap.add_argument("--no-ssh", action="store_true")
	ap.add_argument("--no-tests", action="store_true")
	ap.add_argument("--no-jobs", action="store_true",
	                help="only the tests (with -S: what they say without "
	                "job control)")
	ap.add_argument("--gdb", type=int, default=0,
	                help="QEMU's gdbstub on this port, to look at a hang")
	ap.add_argument("-w", type=int, default=0,
	                help="run N: its own WORK_DIR (work-jobs-N) and ports, "
	                "so that several runs go at once")
	a = ap.parse_args()

	global WORK, PORT, SSHPORT
	if a.w:
		WORK = "%s-%d" % (WORK, a.w)
		PORT += a.w
		SSHPORT += a.w
	os.makedirs(WORK, exist_ok=True)

	libc = [os.path.basename(f) for f in glob.glob(os.path.join(CC, "libc.so.*"))]
	need = ["sh", "pty", "test95", "test96", "jobprobe"]
	for f in need:
		if not os.path.exists(os.path.join(CC, f)):
			sys.exit("no %s/%s: port/test/jobctl/build-jobs.sh first" %
			         (CC, f))
	if len(libc) != 1:
		sys.exit("want one libc.so.* in %s: %s" % (CC, libc))
	libc = libc[0]

	srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Quiet)
	threading.Thread(target=srv.serve_forever, daemon=True).start()
	env = dict(os.environ, WORK_DIR=WORK, QEMU_TIMEOUT="3600",
	           QEMU_SMP=str(a.c), QEMU_HOSTFWD=str(SSHPORT))
	if not a.shared:
		imgs = []
		for name, rel in (("pm", "minix/servers/pm/pm"),
		                  ("mib", "minix/servers/mib/mib"),
		                  ("tty", "minix/drivers/tty/tty/tty")):
			f = os.path.join(VOBJ, rel)
			if not os.path.exists(f):
				sys.exit("no %s: port/test/jobctl/build-jobs.sh first"
				         % f)
			imgs.append("%s=%s" % (name, f))
		env["BOOT_IMAGES"] = " ".join(imgs)
	if a.f:
		env["DISK_FRESH"] = "1"
	if a.gdb:
		env["QEMU_EXTRA"] = (env.get("QEMU_EXTRA", "") +
		                     " -gdb tcp::%d" % a.gdb).strip()
	args = ["bash", RAMIMAGE, "-d"] + (["-2"] if a.el2 else []) + \
	    (["-3"] if a.gic3 else [])
	log = open(os.path.join(WORK, "qemu-jobs.log"), "w")
	p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
	                     stderr=subprocess.STDOUT, bufsize=0, env=env,
	                     start_new_session=True)
	t = Term(p, log)
	sshp = None
	try:
		# The first boot puts the new programs in place.  Renamed into
		# place, not written over: the running shell's text is paged
		# in from its file.
		t.expect(r"login:", 900)
		t.send("root")
		t.expect(r"# ", 60)
		get = ["sh:/bin/sh", "pty:/service/pty",
		       "%s:/usr/lib/%s" % (libc, libc),
		       "test95:/usr/tests/minix-posix/test95",
		       "test96:/usr/tests/minix-posix/test96",
		       "jobprobe:/tmp/jobprobe"]
		for g in get:
			src, dst = g.split(":")
			t.ask("ftp -o /tmp/%s.new http://10.0.2.2:%d/%s >/dev/null "
			      "2>&1" % (src, PORT, src))
			out = t.ask("chmod 755 /tmp/%s.new && mv /tmp/%s.new %s && "
			            "echo GOT" % (src, src, dst))
			if "GOT" not in out:
				raise RuntimeError("%s did not arrive: %r" % (src, out))
		t.ask("sync")
		t.send("/sbin/reboot")

		t.expect(r"login:", 900)
		t.send("root")
		t.expect(r"# ", 60)
		print("QEMU: %d core(s)%s%s, PM, mib and tty %s" % (a.c,
		      ", EL2" if a.el2 else "", ", GICv3" if a.gic3 else "",
		      "shared" if a.shared else "from " + VOBJ))
		if not a.no_jobs:
			jobs(t, "cons")

		if not a.no_ssh and not a.no_jobs:
			pub = open(KEY + ".pub").read().strip()
			t.ask("mkdir -p -m 700 /root/.ssh; echo '%s' > "
			      "/root/.ssh/authorized_keys; chmod 600 "
			      "/root/.ssh/authorized_keys" % pub)
			out = t.ask("[ -f /etc/ssh/ssh_host_ed25519_key ] || "
			            "ssh-keygen -q -t ed25519 -N '' -f "
			            "/etc/ssh/ssh_host_ed25519_key; /usr/sbin/sshd; "
			            "echo SSHD $?", 300)
			if "SSHD 0" not in out:
				raise RuntimeError("sshd: %r" % out)
			time.sleep(3)
			slog = open(os.path.join(WORK, "qemu-jobs-ssh.log"), "w")
			sshp = subprocess.Popen(
			    ["ssh", "-tt", "-i", KEY, "-p", str(SSHPORT),
			     "-o", "BatchMode=yes",
			     "-o", "UserKnownHostsFile=/dev/null",
			     "-o", "StrictHostKeyChecking=no",
			     "-o", "ConnectTimeout=60", "root@127.0.0.1"],
			    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
			    stderr=subprocess.STDOUT, bufsize=0)
			st = Term(sshp, slog)
			st.expect(r"# ", 120)
			tty = st.ask("tty").strip()
			check("ssh", "pts", tty.startswith("/dev/pts/"),
			      "tty: %r" % tty)
			jobs(st, "ssh")
			st.send("exit")
			try:
				sshp.wait(timeout=30)
			except subprocess.TimeoutExpired:
				pass

		if not a.no_tests:
			for n in ("95", "96"):
				out = t.ask("cd /tmp && /usr/tests/minix-posix/test%s; "
				            "echo EXIT $?; cd /" % n, 1200)
				check("cons", "test" + n, ("Test %s ok" % n) in out and
				      "EXIT 0" in out, oneline(out)[-300:])
			out = t.ask("cd /usr/tests/minix-posix && ./run -t '5 37 95 "
			            "96'; cd /", 2400)
			ok = all(re.search(r"Test +%s ok" % n, out)
			         for n in ("5", "37", "95", "96"))
			check("cons", "run", ok and "abort" not in out and
			      "error" not in out, oneline(out)[-300:])

		t.send("/sbin/poweroff")
		t.expect(r"halted|terminated|Power", 120)
	except RuntimeError as e:
		check("-", "run", False, str(e))
	finally:
		if sshp is not None and sshp.poll() is None:
			sshp.kill()
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
