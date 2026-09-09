#!/usr/bin/env python3
"""Из чего складывается время в ядре: счётчики, а не доли.

Зачем. `kern.cp_time` говорит, какая доля времени машины ушла в user-режим,
в системные процессы и в ядро, а `psinfo` говорит то же по процессу, разделяя
собственное время, IPC и системные вызовы. Ни то ни другое не говорит,
**сколько чего было**. Доля без числа не делится на цену и количество, и пока
не разделена, «IPC дорог» — мнение, а не измерение.

Ядро, собранное с `KTRACE` (`minix/kernel/debug.h`), считает: каждый вход,
каждый примитив IPC, каждый системный вызов по номеру, события, из которых
состоит переключение, и кто вызвал каждое пересечение. Рядом с каждым входом
— такты, проведённые в ядре до выхода через него; это деление бесплатно,
потому что то же число `context_stop()` считает для своего учёта.
Читается всё одним файлом — `/proc/ktrace`.

Этот стенд снимает его до и после окна с нагрузкой, вычитает и печатает
разложение. Ходит по тексту, а не по секундам, и все точки снимает одной
загрузкой — как `port/test/smp/qemu-occ.py`, откуда взята и проверенная там
механика (ждать текста; замер гнать файлом; убедиться, что прошлый QEMU умер;
метку печатать разорванной, чтобы `expect` не нашёл её в собственном эхе).

Две вещи, без которых числа будут правдоподобны и неверны:

  своя цена у прибора  Снимок `/proc/ktrace` сам делает системные вызовы и
                       обмены, и его работа попадает в окно. Поэтому точка
                       `idle` — не украшение: она и есть цена прибора, и её
                       надо вычитать глазами из всех остальных.

  такт != инструкция   Под TCG время не настоящее. Ключ -i запускает QEMU с
                       `-icount shift=0` на одном ядре: виртуальные часы идут
                       на наносекунду за инструкцию гостя, CNTVCT_EL0 (то,
                       чем ядро считает такты) становится счётчиком
                       инструкций, и «тактов на вход» превращается в
                       «инструкций на вход» - число, сравнимое с 93
                       инструкциями входа из этапа 3.

	qemu-ipc.py [-k нагрузка,...] [-w окно] [-n итераций] [-i] [-c ядер]
"""

import argparse
import os
import re
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
RAMIMAGE = os.environ.get("RAMIMAGE", os.path.join(HOME, "bin", "ramimage.sh"))
SRC = os.environ.get("MINIXSRC", os.path.join(HOME, "minix-src"))

# Нагрузки. Каждая - одна строка шелла, запускаемая в фоне столько раз,
# сколько задач просят; $N - число итераций.
#
# Почему именно эти. `sh` - тот самый счётный цикл, которым мерили занятость
# ядер и который счётным не оказался: 5.4 % времени в user-режиме. `awk` -
# его противоположность, один exec и дальше чистая интерпретация. Между ними
# лежит всё остальное, и две крайности нужны, чтобы понять, где именно.
#
# Второе число - во сколько раз этой нагрузке нужно больше итераций, чем
# задано ключом -n. Оно не косметическое: оборот шеллового цикла стоит
# двухсот оборотов awk и одной пятитысячной от fork+exec, и одинаковое N
# означало бы, что часть точек кончается через десятую долю секунды после
# начала восьмисекундного окна. Такая точка не выглядит испорченной - она
# выглядит как idle, и первый прогон этого стенда так и намерил "awk" не
# отличающимся от простоя. Отсюда же метка JOBDONE ниже.
JOBS = {
	"idle": (None, 1),
	"sh":   ("n=0; while [ $n -lt $N ]; do n=$((n+1)); done", 1),
	"awk":  ('awk -v n=$N "BEGIN { while (i < n) i++ }"', 400),
	# fork+exec: цена создания процесса, в которой участвуют PM, VM и VFS.
	"fork": ("n=0; while [ $n -lt $N ]; do /usr/bin/true; n=$((n+1)); done",
	         0.01),
	# Файловая система: VFS, MFS и блочный драйвер на каждой записи.
	"io":   ("dd if=/dev/zero of=/tmp/ipcbench.$$ bs=4096 count=$N "
	         "2>/dev/null; rm -f /tmp/ipcbench.$$", 0.5),
	# Труба: два процесса и PFS между ними.
	"pipe": ("dd if=/dev/zero bs=4096 count=$N 2>/dev/null | cat > /dev/null",
	         0.5),
}

ENTRY_ORDER = ("ipc", "kcall", "irq", "irq_idle", "fault", "fpu", "other")


def kcall_names(src):
	"""Имена системных вызовов - из com.h, а не из своей таблицы.

	Второй список имён - это способ получить два списка, которые
	разойдутся. Здесь он читается из того же файла, по которому
	собирается ядро.
	"""
	names = {}
	path = os.path.join(src, "minix", "include", "minix", "com.h")
	try:
		text = open(path, errors="replace").read()
	except OSError:
		return names
	for m in re.finditer(r"#\s*define\s+(SYS_\w+)\s*\(KERNEL_CALL\s*\+\s*(\d+)\)",
	                     text):
		names[int(m.group(2))] = m.group(1)[4:].lower()
	return names


IPC_NAMES = {1: "send", 2: "receive", 3: "sendrec", 4: "notify",
             5: "sendnb", 6: "kerninfo", 16: "senda"}


class Guest:
	"""QEMU за трубой: писать строки, ждать строк."""

	def __init__(self, cpus, timeout, log, extra=None):
		env = dict(os.environ)
		env["QEMU_SMP"] = str(cpus)
		env["QEMU_TIMEOUT"] = str(timeout)
		if extra:
			env["QEMU_EXTRA"] = extra
		self.log = open(log, "w", errors="replace")
		self.p = subprocess.Popen(
		    ["bash", RAMIMAGE, "-d"],
		    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		    stderr=subprocess.STDOUT, env=env, bufsize=0)

	def expect(self, what, timeout):
		deadline = time.time() + timeout
		out = ""
		while time.time() < deadline:
			if self.p.poll() is not None:
				rest = self.p.stdout.read()
				if rest:
					out += rest.decode(errors="replace")
					self.log.write(out[-len(rest):])
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

	def drain(self, tag):
		"""Дочитать чужое эхо, чтобы следующий expect не нашёл в нём себя.

		Метка печатается разорванной: шелл её склеивает, эхо терминала -
		нет, так что команда не совпадает сама с собой.
		"""
		self.send("echo %s''-%s" % (tag[:-2], tag[-1]))
		return self.expect(tag, 90)

	def close(self):
		try:
			self.send("/sbin/poweroff")
			self.expect("halted", 60)
		except Exception:
			pass
		for f in (lambda: self.p.stdin.close(),
		          lambda: self.p.wait(timeout=30)):
			try:
				f()
			except Exception:
				self.p.kill()
		self.log.close()


def parse_ktrace(text):
	"""Разобрать блок между метками в словари счётчиков."""
	kt = {"entry": {}, "cycles": {}, "ev": {}, "ipc": {}, "kcall": {},
	      "proc": {}, "head": {}}
	for line in text.splitlines():
		f = line.strip().split()
		if not f:
			continue
		if f[0] in ("version", "freq", "tsc") and len(f) == 2:
			try:
				kt["head"][f[0]] = int(f[1])
			except ValueError:
				pass
		elif f[0] == "entry" and len(f) == 4:
			kt["entry"][f[1]] = int(f[2])
			kt["cycles"][f[1]] = int(f[3])
		elif f[0] == "ev" and len(f) == 3:
			kt["ev"][f[1]] = int(f[2])
		elif f[0] == "ipc" and len(f) == 3:
			kt["ipc"][int(f[1])] = int(f[2])
		elif f[0] == "kcall" and len(f) == 3:
			kt["kcall"][int(f[1])] = int(f[2])
		elif f[0] == "proc" and len(f) == 5:
			kt["proc"][(int(f[1]), f[2])] = (int(f[3]), int(f[4]))
	return kt


def parse_cp_time(text):
	for line in text.splitlines():
		if "cp_time" in line and "=" in line:
			v = re.findall(r"(user|nice|sys|intr|idle)\s*=\s*(\d+)", line)
			if len(v) == 5:
				return [int(x) for _, x in v]
	return None


def parse_uptime(text):
	for line in text.splitlines():
		f = line.strip().split()
		if len(f) == 1:
			try:
				return float(f[0])
			except ValueError:
				pass
	return None


def diff(a, b):
	"""b - a по ключам b. Значение - число или пара (ipc, kcall)."""
	out = {}
	for k, v in b.items():
		if isinstance(v, tuple):
			was = a.get(k, (0,) * len(v))
			out[k] = tuple(v[i] - was[i] for i in range(len(v)))
		else:
			out[k] = v - a.get(k, 0)
	return out


def report(name, ta, tb, ka, kb, cpa, cpb, freq, icount, knames):
	dt = tb - ta
	if dt <= 0:
		print("%-5s: окно не разобралось" % name)
		return None

	de = diff(ka["entry"], kb["entry"])
	dc = diff(ka["cycles"], kb["cycles"])
	dv = diff(ka["ev"], kb["ev"])
	di = diff(ka["ipc"], kb["ipc"])
	dk = diff(ka["kcall"], kb["kcall"])
	dp = diff(ka["proc"], kb["proc"])

	entries = sum(de.values())
	cycles = sum(dc.values())

	print("")
	print("=== %s: окно %.2f s ===" % (name, dt))

	if cpa and cpb:
		d = [cpb[i] - cpa[i] for i in range(5)]
		tot = sum(d) or 1
		print("cp_time: user %.1f%%  системные %.1f%%  ядро %.1f%%  "
		      "idle %.1f%%" % (100.0 * d[0] / tot, 100.0 * d[2] / tot,
		                       100.0 * d[3] / tot, 100.0 * d[4] / tot))

	# Такт в инструкции переводится только под -icount, и только там это
	# осмысленно; иначе печатаются такты как есть.
	if icount and freq:
		unit, scale = "инстр", 1e9 / freq
	else:
		unit, scale = "тактов", 1.0

	print("%-9s %10s %10s %9s %8s" %
	      ("вход", "в секунду", "всего", "доля", unit + "/шт"))
	for k in ENTRY_ORDER:
		n, c = de.get(k, 0), dc.get(k, 0)
		if n == 0 and c == 0:
			continue
		print("%-9s %10.0f %10d %8.1f%% %8.0f" %
		      (k, n / dt, n, 100.0 * c / (cycles or 1),
		       scale * c / n if n else 0))
	print("%-9s %10.0f %10d %8.1f%% %8.0f" %
	      ("всего", entries / dt, entries, 100.0,
	       scale * cycles / entries if entries else 0))

	ev = [(v, k) for k, v in dv.items() if v]
	if ev:
		print("события: " + "  ".join(
		    "%s %d" % (k, v) for v, k in sorted(ev, reverse=True)))

	ipc = [(v, k) for k, v in di.items() if v]
	if ipc:
		print("IPC:     " + "  ".join(
		    "%s %d" % (IPC_NAMES.get(k, str(k)), v)
		    for v, k in sorted(ipc, reverse=True)))

	kc = sorted(((v, k) for k, v in dk.items() if v), reverse=True)[:8]
	if kc:
		print("вызовы:  " + "  ".join(
		    "%s %d" % (knames.get(k, str(k)), v) for v, k in kc))

	pr = sorted(((v[0] + v[1], k, v) for k, v in dp.items()
	             if v[0] + v[1] > 0), reverse=True)[:8]
	if pr:
		print("кто:     " + "  ".join(
		    "%s %d/%d" % (k[1], v[0], v[1]) for _, k, v in pr))

	return {"dt": dt, "entries": entries, "cycles": cycles,
	        "de": de, "dc": dc, "dv": dv}


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", "--cpus", type=int, default=1)
	ap.add_argument("-k", "--kinds", default="idle,sh,awk,fork,io,pipe")
	ap.add_argument("-j", "--jobs", type=int, default=1)
	ap.add_argument("-n", "--iters", type=int, default=20000)
	ap.add_argument("-w", "--window", type=int, default=8)
	ap.add_argument("-i", "--icount", action="store_true",
	    help="-icount shift=0 на одном ядре: такты становятся инструкциями")
	ap.add_argument("-b", "--bench", action="store_true",
	    help="только время фиксированной работы: чем мерится цена самого "
	         "прибора, потому что это число есть и в ядре без KTRACE")
	ap.add_argument("-l", "--log", default="/tmp/qemu-ipc.log")
	args = ap.parse_args()

	kinds = [k for k in args.kinds.split(",") if k]
	for k in kinds:
		if k not in JOBS:
			sys.exit("нет такой нагрузки: %s" % k)

	if subprocess.call(["pgrep", "-f", "qemu-system-aarch6[4]"],
	    stdout=subprocess.DEVNULL) == 0:
		sys.exit("другой QEMU ещё жив; он держит образ диска")

	extra, cpus = None, args.cpus
	if args.icount:
		# Одно ядро - не удобство: под icount виртуальные часы одни на
		# машину, и на нескольких ядрах наносекунда перестаёт быть
		# инструкцией гостя.
		extra, cpus = "-icount shift=0", 1

	# Щедро: под icount гость идёт в разы медленнее, а ядро паркуется на
	# wfi и само QEMU не выйдет.
	slow = 8 if args.icount else 1
	budget = 300 * slow + sum((60 + 20 * args.window) * slow for _ in kinds)

	knames = kcall_names(SRC)
	g = Guest(cpus, budget, args.log, extra)
	try:
		g.expect("login:", 240 * slow)
		g.send("root")
		g.expect("# ", 90 * slow)

		if args.bench:
			#
			# Одно число и никаких счётчиков: сколько секунд
			# занимает фиксированная работа. Оно есть в обеих
			# сборках ядра, и разница между ними и есть цена
			# прибора. Три круга, потому что под TCG шум хоста
			# виден и в этом.
			#
			g.send("rm -f /tmp/bench.sh")
			for line in [
			    "N=$1",
			    "a=`cat /proc/uptime`",
			    "n=0",
			    "while [ $n -lt $N ]; do n=$((n+1)); done",
			    "b=`cat /proc/uptime`",
			    # Двойные кавычки: строка едет в госте внутри
			    # echo '...', и одинарная в ней рвёт обёртку - а
			    # рвёт она её так, что шелл входа падает с
			    # "longjmp botch" и дампом.
			    'awk -v n=$N "BEGIN { while (i < n) i++ }"',
			    "c=`cat /proc/uptime`",
			    "echo BENCH $a $b $c",
			]:
				g.send("echo '%s' >> /tmp/bench.sh" % line)
			g.drain("SETUP-1")
			for i in range(3):
				g.send("sh /tmp/bench.sh %d" % args.iters)
				out = g.expect("BENCH ", 300 * slow)
				out = g.expect("\n", 300 * slow)
				f = out.strip().split()
				if len(f) >= 3:
					a, b, c = (float(x) for x in f[:3])
					print("круг %d: sh %.2f s  awk %.2f s"
					      % (i + 1, b - a, c - b))
				g.drain("ROUND-1")
			return

		# Замер файлом: канонический буфер tty режет строку около 255
		# байт, а замер длиннее.
		g.send("rm -f /tmp/ipc.sh")
		for line in [
		    "J=$1",
		    "N=$2",
		    "W=$3",
		    "K=$4",
		    "echo MARK-A",
		    "cat /proc/uptime",
		    "sysctl kern.cp_time",
		    "cat /proc/ktrace",
		    "echo MARK-END",
		    "j=0",
		    "while [ $j -lt $J ]; do",
		    # Метка окончания задачи. Без неё точка, чья нагрузка
		    # добежала до конца в первую секунду окна, выглядит
		    # ровно как простой - и читается как результат.
		    "  ( eval \"$K\"; echo JOBDONE ) &",
		    "  j=$((j+1))",
		    "done",
		    "sleep $W",
		    "echo MARK-B",
		    "cat /proc/uptime",
		    "sysctl kern.cp_time",
		    "cat /proc/ktrace",
		    "echo MARK-END",
		    "wait",
		    "echo MARK-DONE",
		]:
			g.send("echo '%s' >> /tmp/ipc.sh" % line)
		g.drain("SETUP-1")

		print("cpus=%d jobs=%d iters=%d window=%ds%s" %
		      (cpus, args.jobs, args.iters, args.window,
		       " icount" if args.icount else ""))

		freq = None
		for kind in kinds:
			job, mul = JOBS[kind]
			g.send("sh /tmp/ipc.sh %d %d %d '%s'" %
			       (args.jobs, max(1, int(args.iters * mul)),
			        args.window,
			        job or ":"))
			out = g.expect("MARK-DONE", (180 + 40 * args.window) * slow)
			a = out.split("MARK-A", 1)[1].split("MARK-END", 1)[0]
			b = out.split("MARK-B", 1)[1].split("MARK-END", 1)[0]
			ka, kb = parse_ktrace(a), parse_ktrace(b)
			# Нагрузка, добежавшая до конца внутри окна, мерилась
			# наполовину простоем. Метка ставится самой задачей.
			if kind != "idle" and "JOBDONE" in \
			    out.split("MARK-A", 1)[1].split("MARK-B", 1)[0]:
				print("%s: задача кончилась внутри окна - "
				      "точка не годится, нужно больше -n"
				      % kind)
			if freq is None:
				freq = kb["head"].get("freq")
				print("freq %s" % freq)
			if not kb["entry"]:
				print("%s: /proc/ktrace пуст - ядро собрано "
				      "без KTRACE?" % kind)
				g.drain("ROUND-1")
				continue
			report(kind, parse_uptime(a), parse_uptime(b), ka, kb,
			       parse_cp_time(a), parse_cp_time(b), freq,
			       args.icount, knames)
			g.drain("ROUND-1")
	finally:
		g.close()
		print("\nжурнал: %s" % args.log)


if __name__ == "__main__":
	sys.exit(main())
