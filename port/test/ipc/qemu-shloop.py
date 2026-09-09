#!/usr/bin/env python3
"""Что именно делает шелловский счётный цикл на каждом обороте.

`/proc/ktrace` сказал, что цикл `while [ $n -lt $N ]; do n=$((n+1)); done`
стоит примерно одного обмена на оборот, и что обмен идёт в VFS, а из VFS -
в драйвер, которого в этом цикле быть не должно вовсе. Это утверждение о
числе, а не о причине. Здесь причина спрашивается у самой системы: `trace(1)`
называет системный вызов, `/proc/dmap` называет драйвер, а счётчики,
снятые вокруг трёх вариантов одного цикла, показывают, от чего зависит цена.

Отдельным файлом, а не ключом к qemu-ipc.py, по той же причине, по какой
замер гонится файлом: цикл содержит `$n`, `$((...))` и кавычки, и провезти
его через wsl.exe в командной строке нельзя - проверено, `$n` доезжает
пустым, а `[` отвечает "unexpected operator". Здесь он строится в госте
построчно.

И метка конца шага печатается разорванной (`SEEN''-1`): шелл её склеивает,
а эхо терминала - нет, поэтому ждать можно ответа, а не своего ввода. Без
этого `expect("# ")` находит приглашение, оставшееся от предыдущей команды,
и следующий шаг разбирается по обрывкам двух разных.
"""

import os
import re
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
RAMIMAGE = os.environ.get("RAMIMAGE", os.path.join(HOME, "bin", "ramimage.sh"))

ITERS = 400
BIG = 4000


class Guest:
	def __init__(self, timeout, log):
		env = dict(os.environ)
		env["QEMU_SMP"] = "1"
		env["QEMU_TIMEOUT"] = str(timeout)
		self.log = open(log, "w", errors="replace")
		self.p = subprocess.Popen(
		    ["bash", RAMIMAGE, "-d"],
		    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		    stderr=subprocess.STDOUT, env=env, bufsize=0)
		self.tag = 0

	def expect(self, what, timeout):
		deadline = time.time() + timeout
		out = ""
		while time.time() < deadline:
			if self.p.poll() is not None:
				raise RuntimeError("QEMU вышел, не дождавшись %r" % what)
			ch = self.p.stdout.read(1)
			if not ch:
				continue
			out += ch.decode(errors="replace")
			self.log.write(out[-1])
			self.log.flush()
			if what in out:
				return out
		raise RuntimeError("не дождался %r за %.0f с" % (what, timeout))

	def send(self, line):
		self.p.stdin.write((line + "\n").encode())
		self.p.stdin.flush()
		time.sleep(0.35)

	def run(self, cmd, timeout=180):
		"""Выполнить и вернуть вывод до разорванной метки."""
		self.tag += 1
		mark = "DONE%d" % self.tag
		self.send(cmd)
		self.send("echo %s''%s" % (mark[:-1], mark[-1]))
		out = self.expect(mark + "\r", timeout)
		return out

	def close(self):
		try:
			self.send("/sbin/poweroff")
			self.expect("halted", 60)
		except Exception:
			pass
		try:
			self.p.stdin.close()
			self.p.wait(timeout=30)
		except Exception:
			self.p.kill()
		self.log.close()


def loop_lines(n):
	return ["n=0", "while [ $n -lt %d ]" % n, "do", "n=$((n+1))", "done"]


def proc_counts(text):
	"""Строки `proc <slot> <имя> <ipc> <kcall>` из /proc/ktrace."""
	out = {}
	for line in text.splitlines():
		f = line.strip().split()
		if len(f) == 5 and f[0] == "proc":
			out[f[2]] = (int(f[3]), int(f[4]))
	return out


def main():
	if subprocess.call(["pgrep", "-f", "qemu-system-aarch6[4]"],
	    stdout=subprocess.DEVNULL) == 0:
		sys.exit("другой QEMU ещё жив; он держит образ диска")

	g = Guest(600, "/tmp/qemu-shloop.log")
	try:
		g.expect("login:", 240)
		g.send("root")
		g.expect("# ", 90)

		for name, n in (("loop.sh", ITERS), ("big.sh", BIG)):
			g.send("rm -f /tmp/%s" % name)
			for line in loop_lines(n):
				g.send("echo '%s' >> /tmp/%s" % (line, name))
		g.run("wc -l /tmp/loop.sh /tmp/big.sh")

		# Кто есть кто: major -> драйвер. Без этого счётчик "кто" даёт
		# имя процесса, но не отвечает, почему он там оказался.
		print("--- /proc/dmap ---")
		print(g.run("cat /proc/dmap"))
		print("--- what fd 0 is ---")
		print(g.run("ls -l /dev/console /dev/tty00 /dev/null /dev/zero"))

		# Что именно повторяется.
		g.run("trace -o /tmp/t.out /bin/sh /tmp/loop.sh", 300)
		print("--- syscalls per %d iterations ---" % ITERS)
		print(g.run("sed -e 's/(.*//' /tmp/t.out | sort | uniq -c | "
		            "sort -rn | head -8"))

		# На что приходится пара ioctl: на оборот цикла или на команду.
		# Разница существенная: в первом случае дорог цикл, во втором -
		# любая команда шелла, то есть всё, что шелл делает вообще.
		# Плоские файлы строятся в госте циклом, а не двумястами echo с
		# этой стороны: каждая строка через последовательную консоль
		# стоит треть секунды.
		variants = (
		    ("flat-test", "[ 1 -lt 2 ]"),	# та же команда, без цикла
		    ("flat-colon", ":"),		# самая пустая из встроенных
		    ("flat-var", "x=1"),		# и вовсе не команда
		)
		# Рядом с числом совпадений - длина файла целиком. Без неё ноль
		# совпадений и пустой файл выглядят одинаково, а это ровно та
		# форма отказа, на которой прибор не жалуется.
		print("--- TIOCGETA / всего строк trace, на %d ---" % ITERS)
		print("%-11s %s" % ("loop", g.run(
		    "grep -c TIOCGETA /tmp/t.out; wc -l < /tmp/t.out").strip()))
		for name, line in variants:
			g.run("rm -f /tmp/v.sh; n=0; while [ $n -lt %d ]; do "
			      "echo '%s' >> /tmp/v.sh; n=$((n+1)); done" %
			      (ITERS, line), 300)
			g.run("wc -l < /tmp/v.sh", 60)
			g.run("trace -o /tmp/v.out /bin/sh /tmp/v.sh", 600)
			print("%-11s %s" % (name, g.run(
			    "grep -c TIOCGETA /tmp/v.out; wc -l < /tmp/v.out")
			    .strip()))

		# И от чего зависит цена: тот же цикл с разным stdin.
		for what, redir in (("console", ""),
		                    ("/dev/null", "< /dev/null"),
		                    ("closed", "<&-")):
			a = g.run("cat /proc/ktrace")
			g.run("/bin/sh /tmp/big.sh %s" % redir, 300)
			b = g.run("cat /proc/ktrace")
			pa, pb = proc_counts(a), proc_counts(b)
			row = []
			for name in sorted(set(pa) | set(pb)):
				d = (pb.get(name, (0, 0))[0] - pa.get(name, (0, 0))[0],
				     pb.get(name, (0, 0))[1] - pa.get(name, (0, 0))[1])
				if d[0] + d[1] > BIG // 8:
					row.append("%s %d/%d" % (name, d[0], d[1]))
			print("stdin %-10s %s" % (what, "  ".join(row)))
	finally:
		g.close()
		print("журнал: /tmp/qemu-shloop.log")


if __name__ == "__main__":
	sys.exit(main())
