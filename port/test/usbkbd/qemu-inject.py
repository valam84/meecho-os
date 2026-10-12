#!/usr/bin/env python3
"""Keys into the console line, on QEMU: the half of the USB keyboard that
has nothing to do with USB.

QEMU's virt machine has no xHCI this port drives, so the keyboard itself
is the board's business.  Everything above it is not: usb_kbd's reports
(report.c), libinputdriver, the input server, tty's keymap and modifiers
(arch/aarch64/keyboard.c), the console line, its echo and its signals.
usb_kbd's inject=TEXT types TEXT through exactly that path, and this
script checks what comes out at the console - which is the serial line
QEMU gives us, the one the keyboard types into.

Each check types a command whose OUTPUT differs from its text, so that
the echo of what was typed is not mistaken for it having run:

  printf 'K%sB\\n' OK      -> KOKB    shift, symbols, Enter
  ... XY<BS>Q              -> BXQZ    backspace edits the line
  sleep 30, then ^C, then  -> CXC     control, and ISIG: it must come
  printf 'C%sC\\n' X                   back in seconds, not after 30
  logout; keys type root   -> LOGIN   getty and login take the keyboard

Runs in its own WORK_DIR (~/obj-evbarm64/work-kbd) with its own disk, so
that it can run beside another session's QEMU.  Log: ~/qemu-inject.log.

    python3 port/test/usbkbd/qemu-inject.py [-k] [-3] [-s N]

-k keeps the disk image from the last run instead of remaking it.
"""

import argparse
import os
import select
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
WORK = os.path.expanduser("~/obj-evbarm64/work-kbd")


def esc(text):
    """inject= wants its argument without spaces or shell quoting."""
    out = []
    for c in text:
        if c.isalnum() or c in "-_.,/:=+@":
            out.append(c)
        else:
            out.append("%%%02x" % ord(c))
    return "".join(out)


class Guest(object):
    def __init__(self, log, args):
        env = dict(os.environ)
        env["WORK_DIR"] = WORK
        env["QEMU_TIMEOUT"] = "600"
        env["QEMU_SMP"] = str(args.s)
        if not args.k:
            env["DISK_FRESH"] = "1"
        cmd = ["bash", RAMIMAGE, "-d"] + (["-3"] if args.gic3 else [])
        self.log = log
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, bufsize=0,
                                  env=env, start_new_session=True)
        self.buf = ""

    def expect(self, what, timeout):
        deadline = time.time() + timeout
        while True:
            i = self.buf.find(what)
            if i >= 0:
                out = self.buf[:i + len(what)]
                self.buf = self.buf[i + len(what):]
                return out
            if time.time() >= deadline:
                raise RuntimeError("no %r in %.0f s" % (what, timeout))
            if self.p.poll() is not None:
                raise RuntimeError("QEMU exited waiting for %r" % what)
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
        self.p.stdin.write((line + "\n").encode())
        self.p.stdin.flush()
        time.sleep(0.35)

    def stop(self):
        # Our own process group and our own QEMU only: the image path
        # is in our WORK_DIR, and nothing else matches it.
        try:
            os.killpg(os.getpgid(self.p.pid), signal.SIGKILL)
        except Exception:
            pass
        subprocess.call(["pkill", "-9", "-f", WORK + "/kernel.bin"])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-k", action="store_true", help="keep the disk image")
    ap.add_argument("-3", dest="gic3", action="store_true", help="GICv3")
    ap.add_argument("-s", type=int, default=1, help="cores")
    a = ap.parse_args()

    os.makedirs(WORK, exist_ok=True)
    log = open(os.path.expanduser("~/qemu-inject.log"), "w")
    g = Guest(log, a)
    results = []

    def check(name, want, timeout, then_prompt=True):
        try:
            g.expect(want, timeout)
            if then_prompt:
                g.expect("# ", 20)
            results.append((name, True))
            print("ok    %s" % name)
        except RuntimeError as e:
            results.append((name, False))
            print("FAIL  %s: %s" % (name, e))

    def kbd_cmd(label, text, wait=1):
        return ("minix-service up /service/usb_kbd -label %s -args "
                "'inject=%s injectwait=%d'" % (label, esc(text), wait))

    def kbd(label, text, wait=1):
        g.send(kbd_cmd(label, text, wait))

    def down(*labels):
        # Each instance holds one of the input server's keyboard slots
        # for as long as it runs; there are four.  Stopping it is also
        # the check of the SIGTERM path.
        g.send("; ".join("minix-service down %s" % l for l in labels) +
               "; printf 'DO%sWN\\n' NE")
        g.expect("DONEWN", 30)
        g.expect("# ", 20)

    try:
        g.expect("login:", 240)
        g.send("root")
        g.expect("# ", 60)
        g.send("PATH=$PATH:/sbin:/usr/sbin; export PATH")
        g.expect("# ", 20)

        # The input server is what hands key events to tty; on aarch64
        # nothing started it before the keyboard.
        g.send("minix-service up /service/input -dev /dev/kbdmux; "
               "printf 'IN%sUP\\n' PUT")
        check("the input server starts", "INPUTUP", 30)

        kbd("kbdi1", "printf 'K%sB\\n' OK\n")
        check("shift, symbols and Enter: printf 'K%sB\\n' OK", "KOKB", 30)
        down("kbdi1")

        kbd("kbdi2", "printf 'B%sZ\\n' XY\bQ\n")
        check("backspace edits the line", "BXQZ", 30)
        down("kbdi2")

        # Two keyboards: one types the command, the other ^C three
        # seconds later, when sleep is surely in the foreground - and
        # then a command that shows it came back.
        g.send(kbd_cmd("kbdi3", "sleep 30\n") + "; " +
               kbd_cmd("kbdi4", "\x03printf 'C%sC\\n' X\n", wait=4))
        check("control: ^C stops sleep 30 at once", "CXC", 15)
        down("kbdi3", "kbdi4")

        # Out, and back in by the keyboard: getty asks, the keys answer.
        kbd("kbdi5", "root\n", wait=6)
        g.send("exit")
        check("getty takes the login name from the keyboard", "login:",
              30, then_prompt=False)
        check("and login gives a shell", "# ", 30, then_prompt=False)
        g.send("PATH=$PATH:/sbin:/usr/sbin; export PATH; "
               "printf 'L%sN\\n' OGI")
        check("the shell answers on the serial line", "LOGIN", 20)
        down("kbdi5")

        g.send("/sbin/poweroff")
        time.sleep(3)
    finally:
        g.stop()

    bad = [n for n, ok in results if not ok]
    print("\n%d check(s), %d failed" % (len(results), len(bad)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
