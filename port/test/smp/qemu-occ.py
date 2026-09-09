#!/usr/bin/env python3
"""Занятость ядер на QEMU: та же метрика, что и на плате, одной загрузкой.

Зачем. На CB2 потолок занятости — около 1.9 ядра из четырёх, и добавление
задач его не поднимает. Вопрос «это свойство платы или свойство планировщика»
остался без ответа, потому что снять то же число на эмуляторе не вышло:
расписание шло вслепую по секундам и промахивалось мимо приглашения. Здесь
оно ходит по тексту.

Три вещи, из-за которых прошлый стенд не доехал, и что с ними сделано
(PORTING-LOG.md, веха 8.0.3, «Занятость на QEMU снять не вышло»):

  ждать текста, а не секунд   загрузка бывает длиннее обычной, и тогда
                              команда уходит в никуда. Здесь каждый шаг ждёт
                              своей строки с предельным сроком.
  замер гнать файлом          канонический буфер tty режет строку около 255
                              байт, а замер длиннее. Скрипт собирается в
                              госте построчно и запускается как файл.
  убедиться, что прошлый умер два экземпляра QEMU не делят образ диска, и
                              второй отказывается со «Failed to get write
                              lock». Проверяется здесь, до запуска.

Корень берётся с диска, а не ramdisk, и это не вкусовщина: в ramdisk-корне
**нет `sleep`**, а замер весь построен на окне реального времени. Первый
прогон этого стенда получил окно в 0.24 секунды и три правдоподобных числа —
`sleep: not found` печаталось в консоль и в глаза не бросалось, потому что
искали не его. Корень с диска несёт полный userland и заодно ближе к тому, на
чём мерилась плата.

Одна загрузка меряет все точки подряд: перезагружаться ради каждой не нужно,
а сравнивать точки между собой законно только внутри одной конфигурации.

Сравнивать QEMU с четырьмя ядрами и QEMU с одним между собой нельзя вовсе:
под TCG одиночная задача замедляется, когда эмулятор крутит четыре потока
vCPU. Занятость этого и не требует — она считается внутри одного прогона, и
одноядерный прогон нужен ровно за тем же, за чем он был нужен на плате: как
проверка самой метрики. Там она должна дать единицу при любом числе задач.

	qemu-occ.py [-c ядер] [-j 1,2,3,4,6,8] [-n итераций] [-w окно]
"""

import argparse
import os
import re
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
RAMIMAGE = os.environ.get("RAMIMAGE", os.path.join(HOME, "bin", "ramimage.sh"))


class Guest:
	"""QEMU за трубой: писать строки, ждать строк.

	Ждём именно текста. Единственное место, где приходится ждать времени, —
	пауза между отправкой строк: последовательная консоль не отвечает на
	каждую, и торопиться некуда.
	"""

	def __init__(self, cpus, timeout, log):
		env = dict(os.environ)
		env["QEMU_SMP"] = str(cpus)
		env["QEMU_TIMEOUT"] = str(timeout)
		self.log = open(log, "w", errors="replace")
		self.p = subprocess.Popen(
		    ["bash", RAMIMAGE, "-d"],
		    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		    stderr=subprocess.STDOUT, env=env, bufsize=0)
		self.buf = ""

	def expect(self, what, timeout):
		"""Читать до появления строки what. Возвращает всё прочитанное."""
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
			c = ch.decode(errors="replace")
			out += c
			self.log.write(c)
			self.log.flush()
			if what in out:
				return out
		raise RuntimeError("не дождался %r за %.0f с" % (what, timeout))

	def send(self, line):
		self.p.stdin.write((line + "\n").encode())
		self.p.stdin.flush()
		time.sleep(0.35)

	def drain(self, tag):
		"""Дочитать всё, что осталось от предыдущих шагов.

		Иначе следующий expect() найдёт своё слово в чужом эхе, и это не
		теория: первый прогон этого стенда «нашёл» MARK-DONE в строке
		`echo 'echo MARK-DONE' >> /tmp/occ.sh`, оставшейся непрочитанной
		от сборки скрипта, — и разобрал замер по обрывкам двух разных.

		Слово-метка печатается разорванным на две части: `SEEN''-1`. Шелл
		склеивает его при исполнении, а эхо терминала показывает с
		кавычками — то есть команда не совпадает сама с собой, и ждать
		можно именно ответа, а не своего же ввода.
		"""
		self.send("echo %s''-%s" % (tag[:-2], tag[-1]))
		return self.expect(tag, 60)

	def close(self):
		try:
			self.send("/sbin/poweroff")
			self.expect("halted", 60)
		except Exception:
			pass
		try:
			self.p.stdin.close()
		except Exception:
			pass
		try:
			self.p.wait(timeout=30)
		except Exception:
			self.p.kill()
		self.log.close()


def parse_block(text):
	"""Из блока «uptime, затем psinfo» — время и сумма cycles.

	psinfo десятым полем несёт cycles; строки процессов начинаются с версии
	формата. Сумма по всем процессам законна потому, что ядро на aarch64
	берёт счётчик из CNTVCT_EL0 — он один на все ядра и идёт с одной
	частотой.
	"""
	t, total, seen = None, 0, 0
	per = {}
	for line in text.splitlines():
		f = line.strip().split()
		if t is None and len(f) == 1:
			try:
				t = float(f[0])
			except ValueError:
				pass
			continue
		if len(f) >= 13 and f[0] == "1":
			try:
				# Три поля, а не одно: 10 - собственное время
				# процесса, 11 и 12 - то, что ядро потратило на
				# его IPC и его системные вызовы. Доля последних
				# двух растёт с нагрузкой, так что пропустить их
				# значит получить потолок там, где его нет.
				c = int(f[9]) + int(f[10]) + int(f[11])
				total += c
				per[(f[2], f[3])] = c
				seen += 1
			except ValueError:
				pass
	return t, total, seen, per


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-c", "--cpus", type=int, default=4)
	ap.add_argument("-j", "--jobs", default="1,2,3,4,6,8")
	ap.add_argument("-n", "--iters", type=int, default=20000)
	ap.add_argument("-w", "--window", type=int, default=6)
	ap.add_argument("-l", "--log", default="/tmp/qemu-occ.log")
	args = ap.parse_args()

	jobs = [int(x) for x in args.jobs.split(",")]

	# Образ диска один, и два QEMU его не делят: второй отказывается со
	# «Failed to get write lock», а расписание при этом выглядит просто
	# промахнувшимся мимо приглашения.
	if subprocess.call(["pgrep", "-f", "qemu-system-aarch6[4]"],
	    stdout=subprocess.DEVNULL) == 0:
		sys.exit("другой QEMU ещё жив; он держит образ диска")
	# Предельный срок QEMU: загрузка плюс каждая точка со своим окном, с
	# запасом. Ядро паркуется на wfi, само оно не выйдет.
	budget = 120 + sum(args.window + 14 for _ in jobs)

	g = Guest(args.cpus, budget, args.log)
	try:
		g.expect("login:", 180)
		g.send("root")
		g.expect("# ", 60)

		# Замер файлом, а не строкой: длинная строка не пережила бы
		# канонический буфер tty. Каждая строка здесь заведомо короткая.
		g.send("rm -f /tmp/occ.sh")
		for line in [
		    "J=$1",
		    "N=$2",
		    "W=$3",
		    "j=0",
		    "while [ $j -lt $J ]; do",
		    "(n=0; while [ $n -lt $N ]; do n=$((n+1)); done) &",
		    "j=$((j+1))",
		    "done",
		    "sleep 2",
		    "echo MARK-A",
		    "cat /proc/uptime",
		    "cat /proc/[0-9]*/psinfo",
		    "echo MARK-END",
		    "sleep $W",
		    "echo MARK-B",
		    "cat /proc/uptime",
		    "cat /proc/[0-9]*/psinfo",
		    "echo MARK-END",
		    "wait",
		    "echo MARK-DONE",
		]:
			g.send("echo '%s' >> /tmp/occ.sh" % line)
		g.send("wc -l /tmp/occ.sh")
		g.drain("SETUP-1")

		print("cpus=%d iters=%d window=%ds" %
		      (args.cpus, args.iters, args.window))
		results = []
		for j in jobs:
			g.send("sh /tmp/occ.sh %d %d %d" % (j, args.iters,
			    args.window))
			out = g.expect("MARK-DONE", 240)
			a = out.split("MARK-A", 1)[1].split("MARK-END", 1)[0]
			b = out.split("MARK-B", 1)[1].split("MARK-END", 1)[0]
			ta, ca, na, pa = parse_block(a)
			tb, cb, nb, pb = parse_block(b)
			if ta is None or tb is None or tb <= ta:
				print("%2d jobs: время не разобралось" % j)
				continue
			dt = tb - ta
			rate = (cb - ca) / dt
			# Сколько досталось самому жадному процессу. Отвечает на
			# вопрос, который сумма скрывает: одна задача занимает
			# ядро целиком или тоже неполно.
			top = sorted(((pb[k] - pa[k]) / dt, k)
			             for k in pb if k in pa)
			results.append((j, rate, nb, top[-1][0] if top else 0))
			print("%2d jobs: %12.0f cyc / %5.2f s = %11.0f cyc/s "
			      "(%d процессов, самый жадный %.0f cyc/s)" %
			      (j, cb - ca, dt, rate, nb, top[-1][0] if top else 0))
			g.drain("ROUND-1")

		if results:
			base = results[0][1]
			# Единица берётся из самого жадного процесса при одной
			# задаче: с одной задачей ровно одно ядро занято работой,
			# и что этот процесс насчитал за секунду - это и есть
			# «одно ядро» в единицах счётчика. Нормировать на сумму
			# нельзя: в неё входит и фон.
			one = results[0][3] or base
			print("\nв ядрах (единица - самый жадный процесс при одной "
			      "задаче, %.0f cyc/s):" % one)
			for j, rate, _, top in results:
				print("%2d jobs: всего %.2f, на задачу %.2f" %
				      (j, rate / one, top / one))
	finally:
		g.close()
		print("\nжурнал: %s" % args.log)


if __name__ == "__main__":
	sys.exit(main())
