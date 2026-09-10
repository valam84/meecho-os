#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Прогнать родной набор тестов MINIX на QEMU и записать, что вышло.

Набор лежит в /usr/tests/minix-posix и запускается своим скриптом `run`.
Гнать его одной командой нельзя: тест, который встал, съедает весь прогон и
уносит с собой результаты остальных девяноста. Поэтому здесь каждый тест
запускается отдельным вызовом `run -T -t N`, и у каждого свой предельный
срок.

Три вещи, унаследованные от port/test/smp/qemu-occ.py и стоившие там времени:

  ждать текста, а не секунд    загрузка бывает длиннее обычной, и команда,
                               посланная по часам, уходит в никуда.
  дочитывать чужое эхо         иначе следующий expect() находит своё слово в
                               эхе предыдущей команды. Метки печатаются
                               разорванными (`D''ONE`): шелл их склеит, а эхо
                               терминала покажет с кавычками.
  убедиться, что прошлый QEMU умер  два экземпляра не делят образ диска.

Что делает стенд с зависшим тестом: шлёт ^C и ждёт приглашения. Если шелл не
ответил и на это, гость считается потерянным, QEMU перезапускается, и прогон
продолжается со следующего теста - иначе одно зависание решало бы за всех.

	qemu-tests.py [-t список] [-T срок] [-o отчёт]
"""

import argparse
import os
import re
import select
import signal
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
RAMIMAGE = os.environ.get("RAMIMAGE", os.path.join(HOME, "bin", "ramimage.sh"))
TESTDIR = "/usr/tests/minix-posix"

# Тесты, которым штатно нужно больше времени: они меряют кэш файловой системы
# или гоняют его на объёме, а под TCG всё дороже примерно на порядок.
SLOW = {"71": 900, "72": 900, "74": 900, "vm": 900, "mfs": 600, "sh2": 600}


def wait_no_qemu(what=45):
	"""Дождаться, пока прежний QEMU отпустит образ диска.

	Два экземпляра его не делят: второй падает с «Failed to get write
	lock» ещё до первой строки ядра, и по журналу это выглядит как
	промах расписания мимо приглашения, а не как занятый файл.
	"""
	for _ in range(what):
		if subprocess.call(["pgrep", "-f", "qemu-system-" + "aarch64"],
		                   stdout=subprocess.DEVNULL) != 0:
			return
		time.sleep(1)
	sys.exit("прежний QEMU всё ещё держит образ диска")


class Guest(object):
	"""QEMU за трубой: писать строки, ждать строк."""

	def __init__(self, log, timeout=7200):
		wait_no_qemu()
		env = dict(os.environ)
		env["QEMU_TIMEOUT"] = str(timeout)
		self.log = log
		self.p = subprocess.Popen(
		    ["bash", RAMIMAGE, "-d"],
		    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
		    stderr=subprocess.STDOUT, bufsize=0, env=env,
		    # Своя группа процессов: под нами не сам QEMU, а
		    # ramimage.sh, который запускает его через timeout(1).
		    # kill() по одному pid убивает оболочку и оставляет QEMU
		    # держать образ диска - следующий прогон получает «Failed
		    # to get write lock» и не грузится вовсе.
		    start_new_session=True)
		self.buf = ""

	def expect(self, what, timeout):
		"""Читать до появления what. Ищет и в том, что уже прочитано.

		Общий буфер здесь не украшение: без него текст, попавший в
		предыдущий expect(), для следующего не существует. Стенд из-за
		этого дважды терял гостя на ровном месте - `login:` успевало
		проехать внутри ожидания приглашения шелла, и следующий шаг
		ждал его же ещё минуту и не дожидался.
		"""
		deadline = time.time() + timeout
		while True:
			i = self.buf.find(what)
			if i >= 0:
				out = self.buf[:i + len(what)]
				self.buf = self.buf[i + len(what):]
				return out
			if time.time() >= deadline:
				raise RuntimeError("не дождался %r за %.0f с"
				                   % (what, timeout))
			if self.p.poll() is not None:
				rest = self.p.stdout.read()
				if rest:
					c = rest.decode(errors="replace")
					self.buf += c
					self.log.write(c)
					continue
				raise RuntimeError("QEMU вышел, не дождавшись %r" % what)
			# select(), не голый read(1): без него чтение блокируется
			# навсегда, когда гость молчит, и предельный срок выше не
			# проверяется вовсе - зависший тест съедает весь прогон.
			if not select.select([self.p.stdout], [], [], 0.2)[0]:
				continue
			ch = self.p.stdout.read(1)
			if not ch:
				continue
			c = ch.decode(errors="replace")
			self.buf += c
			self.log.write(c)
			self.log.flush()

	def send(self, line):
		self.p.stdin.write((line + chr(10)).encode())
		self.p.stdin.flush()
		time.sleep(0.35)

	def interrupt(self):
		self.p.stdin.write(b"")
		self.p.stdin.flush()
		time.sleep(0.5)

	def kill_group(self):
		try:
			os.killpg(os.getpgid(self.p.pid), signal.SIGKILL)
		except Exception:
			pass
		# ...и по командной строке тоже: ramimage.sh запускает QEMU
		# через timeout(1), а тот заводит собственную группу процессов,
		# так что killpg по нашей до него не достаёт. Шаблон берётся по
		# имени образа ядра - под ним ходит только наш гость.
		try:
			subprocess.call(["pkill", "-9", "-f", "kernel.bin"],
			                stdout=subprocess.DEVNULL,
			                stderr=subprocess.DEVNULL)
		except Exception:
			pass

	def alive(self):
		"""Отвечает ли шелл. Метка рвётся кавычками, чтобы эхо
		терминала не совпало с ответом на неё."""
		self.buf = ""
		self.send("echo RE''ADY")
		try:
			self.expect("READY", 20)
			return True
		except Exception:
			return False

	def login(self):
		self.expect("login:", 300)
		self.send("root")
		self.expect("# ", 60)
		self.send("cd %s" % TESTDIR)
		self.expect("# ", 60)

	def login_at_prompt(self):
		"""Войти после того, как система вернулась к login:.

		Пустая строка первой не для красоты: проверка живости шлёт
		`echo ...`, и если шелла уже нет, это слово уходит getty как
		имя пользователя - на экране `Password:`, и «root» следом
		становится паролем. Поэтому сначала домолчать до нового
		приглашения, а уже потом входить.
		"""
		self.buf = ""
		self.send("")
		self.expect("login:", 120)
		self.send("root")
		self.expect("# ", 60)
		self.send("cd %s" % TESTDIR)
		self.expect("# ", 60)

	def close(self):
		try:
			self.send("/sbin/poweroff")
			self.expect("halted", 90)
		except Exception:
			pass
		try:
			self.p.stdin.close()
		except Exception:
			pass
		try:
			self.p.wait(timeout=20)
		except Exception:
			self.kill_group()
		# Дождаться, что процесс действительно ушёл: два QEMU не делят
		# образ диска, и следующий получит «Failed to get write lock»
		# вместо загрузки.
		try:
			self.p.wait(timeout=60)
		except Exception:
			pass
		time.sleep(2)


def one_test(g, name, timeout):
	"""Запустить один тест. Возвращает (итог, вывод)."""
	# Метка конца печатается после run, вместе с его кодом возврата. Слово
	# рвётся кавычками, чтобы эхо терминала не совпало с ответом.
	g.send("./run -T -t %s; echo E''ND-%s=$?" % (name, name))
	try:
		out = g.expect("END-%s=" % name, timeout)
	except RuntimeError:
		# Тест не ответил в срок. Всё, что он успел напечатать, из
		# буфера выбрасывается: TAP-диагностика идёт с префиксом "# ",
		# и ожидание приглашения шелла нашло бы её, объявив живым
		# гостя, который уже умер.
		g.buf = ""
		g.interrupt()
		# ^C по этой консоли роняет сам шелл (longjmp botch), так что
		# «нет ответа» ещё не значит «система встала»: init поднимает
		# новый getty, и достаточно войти снова.
		if g.alive():
			g.send("cd %s" % TESTDIR)
			try:
				g.expect("# ", 20)
			except Exception:
				pass
			return "TIMEOUT", ""
		# Шелла больше нет. Уговаривать getty впустить нас заново
		# оказалось ненадёжно (ответ на `Password:` теряется, и стенд
		# ждёт приглашения, которого уже не будет), а перезапуск гостя
		# стоит полторы минуты и работает всегда.
		return "LOST", ""

	tail = out[out.rfind("END-%s=" % name):]
	m = re.search(r"END-%s=(\d+)" % re.escape(name), tail)
	rc = int(m.group(1)) if m else -1
	if "\nnot ok test" in out or "\r\nnot ok test" in out:
		verdict = "FAIL"
	elif re.search(r"(^|\n|\r)ok test ", out):
		verdict = "PASS"
	elif "warning: skipping" in out or "No test binaries" in out:
		verdict = "MISSING"
	else:
		verdict = "UNKNOWN(rc=%d)" % rc
	return verdict, out


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("-t", "--tests", default=None,
	                help="список через запятую; по умолчанию весь набор")
	ap.add_argument("-T", "--timeout", type=int, default=240)
	ap.add_argument("-o", "--out", default="/tmp/suite-result.txt")
	ap.add_argument("-l", "--log", default="/tmp/suite-console.log")
	args = ap.parse_args()

	if args.tests:
		tests = args.tests.split(",")
	else:
		tests = [str(n) for n in range(1, 95)] + \
		        ["sh1", "sh2", "interp", "mfs", "isofs", "vnd", "rmib"]

	log = open(args.log, "w", errors="replace")
	res = open(args.out, "w")
	g = Guest(log)
	g.login()
	counts = {}
	try:
		for name in tests:
			t0 = time.time()
			verdict, out = one_test(g, name, SLOW.get(name, args.timeout))
			dt = time.time() - t0
			if verdict == "LOST":
				# Гость потерян: поднимаем заново и повторяем тест,
				# чтобы отличить «встал» от «уронил систему».
				g.close()
				log.write("\n=== guest lost on test %s, restarting ===\n" % name)
				g = Guest(log)
				g.login()
				verdict = "HANG"
			line = "%-8s %-10s %6.1fs" % (name, verdict, dt)
			print(line, flush=True)
			res.write(line + "\n")
			res.flush()
			counts[verdict] = counts.get(verdict, 0) + 1
	finally:
		g.close()
		summary = "  ".join("%s=%d" % kv for kv in sorted(counts.items()))
		print("== " + summary)
		res.write("== " + summary + "\n")
		res.close()
		log.close()
	return 0


if __name__ == "__main__":
	sys.exit(main())
