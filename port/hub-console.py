#!/usr/bin/env python3
"""Консоль платы с машины, к которой она подключена по USB-TTL.

Заменяет мост socat + powershell + COM-порт, который был нужен, пока
переходник приезжал на Windows-хост по сети через USB Network Gate. Здесь
переходник воткнут прямо в эту машину, так что ничего изобретать не нужно:
открыть /dev/ttyUSB0, писать всё в журнал и слать команды по расписанию.

Расписание — построчно, "<секунд> <что послать>":

    0                       просто подождать
    3 root                  подождать 3 с и послать строку с CR
    2 <CR>                  подождать 2 с и послать голый CR
    120 WAIT login:         ждать появления текста, но не дольше 120 с
    0 BAUD 115200           сменить скорость порта на лету

Ожидание текста важнее, чем кажется: загрузка идёт то быстрее, то медленнее,
и один сдвиг на пять секунд уводит весь остаток расписания в приглашение
login — уже стоило одного зависшего прогона.

    hub-console.py /dev/ttyUSB0 1500000 /tmp/run.log расписание.txt
    hub-console.py /dev/ttyUSB0 1500000 /tmp/run.log --listen 60
"""

import os
import subprocess
import sys
import threading
import time


def set_line(dev, baud):
    subprocess.run(
        ["stty", "-F", dev, str(baud), "raw", "-echo", "-crtscts",
         "min", "0", "time", "0"],
        check=True)


class Console:
    def __init__(self, dev, baud, logpath):
        self.dev = dev
        self.logpath = logpath
        set_line(dev, baud)
        self.fd = os.open(dev, os.O_RDWR | os.O_NOCTTY)
        self.log = open(logpath, "wb", buffering=0)
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.stop = False
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        while not self.stop:
            try:
                data = os.read(self.fd, 4096)
            except OSError:
                time.sleep(0.05)
                continue
            if data:
                self.log.write(data)
                with self.lock:
                    self.buf.extend(data)
            else:
                time.sleep(0.02)

    def send(self, text):
        os.write(self.fd, text.encode("utf-8", "replace") + b"\r")

    def cr(self):
        os.write(self.fd, b"\r")

    def baud(self, b):
        # Порт настраивается на лету: U-Boot умеет менять скорость посреди
        # загрузки, и терминал обязан пойти за ним.
        set_line(self.dev, b)

    def seen(self, text):
        with self.lock:
            return text.encode("utf-8", "replace") in bytes(self.buf)

    def wait(self, text, limit):
        deadline = time.time() + limit
        while time.time() < deadline:
            if self.seen(text):
                return True
            time.sleep(0.5)
        return False

    def close(self):
        self.stop = True
        time.sleep(0.2)
        os.close(self.fd)
        self.log.close()


def run_schedule(con, lines):
    for raw in lines:
        line = raw.rstrip("\n")
        if not line or line.lstrip().startswith("#"):
            continue
        parts = line.split(" ", 1)
        try:
            delay = float(parts[0])
        except ValueError:
            print("плохая строка расписания: %r" % line, file=sys.stderr)
            continue
        cmd = parts[1] if len(parts) > 1 else ""

        if cmd.startswith("WAIT "):
            want = cmd[5:]
            ok = con.wait(want, delay)
            print(("=== дождался: " if ok else "!!! НЕ дождался: ") + want,
                  file=sys.stderr)
            continue

        time.sleep(delay)
        if cmd.startswith("BAUD "):
            con.baud(int(cmd[5:]))
            print("=== скорость %s" % cmd[5:], file=sys.stderr)
        elif cmd == "<CR>":
            con.cr()
        elif cmd:
            print(">>> " + cmd, file=sys.stderr)
            con.send(cmd)


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    dev, baud, logpath, arg = sys.argv[1], int(sys.argv[2]), sys.argv[3], \
        sys.argv[4]

    con = Console(dev, baud, logpath)
    try:
        if arg == "--listen":
            secs = float(sys.argv[5]) if len(sys.argv) > 5 else 30
            con.cr()
            time.sleep(secs)
        else:
            with open(arg) as f:
                run_schedule(con, f.readlines())
        time.sleep(1)
    finally:
        con.close()

    with open(logpath, "rb") as f:
        data = f.read()
    sys.stdout.write(data.decode("utf-8", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
