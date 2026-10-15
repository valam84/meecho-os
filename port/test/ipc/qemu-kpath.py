#!/usr/bin/env python3
"""What one crossing of the kernel is made of, and who takes the FP traps.

KTRACE (qemu-ipc.py) counts entries and the cycles between context_stop()
on the way in and on the way out. It cannot say which function inside the
crossing the instructions went to, and it cannot say which program and which
instruction took an FP trap. This stand answers both on QEMU, with no change
to the kernel (port/PORTING-LOG.md, "О8").

  tb   Under -icount shift=0, QEMU logs every kernel translation block it
       executes (-d exec,nochain, -dfilter on the kernel's half), switched on
       through the monitor only while the workload runs. A TB is straight-
       line code, so the instructions it ran are the ones from its start up
       to the branch (or system instruction) that ended it - read off the
       kernel's own disassembly. Crossings are cut at the EL0 vectors,
       classified (ipc, kcall, fpu, irq, fault) and summed per function.

       Single-stepping in gdb was tried first and does not work: without
       -icount the time stepping takes is real, the tick loop in
       context_stop() runs a dozen times per crossing and timer interrupts
       come at every resume; with -icount a step takes seconds.

  fp   Without -icount, a gdbstub breakpoint on trap_handler with EC 7 in
       the syndrome: at each FP trap the process (struct proc from x0, so
       the kernel needs -g) and its pc, symbolized against the service or
       program in the object tree / DESTDIR.

    qemu-kpath.py tb  -K KERNEL [-w workload] [-t secs]
    qemu-kpath.py fp  -K KERNEL_WITH_G [-w workload] [-n traps]

Workloads: sh, pipe, io, fork - qemu-ipc.py's, run in the background
after the login settles. Output and the QEMU work directory go to
TRACE_DIR (default ~/obj-evbarm64/kpath).
"""

import argparse
import bisect
import collections
import os
import re
import socket
import subprocess
import sys
import time

HOME = os.path.expanduser("~")
HERE = os.path.dirname(os.path.abspath(__file__))
RAMIMAGE = os.path.join(HERE, "..", "..", "ramimage.sh")
D = os.environ.get("TRACE_DIR", os.path.join(HOME, "obj-evbarm64/kpath"))
XT = os.path.join(HOME, "xtools-aarch64/bin/aarch64-elf64-minix-")
OBJ = os.environ.get("OBJ", os.path.join(HOME, "obj-evbarm64"))
DEST = os.environ.get("DESTDIR", os.path.join(HOME, "dest-evbarm64"))

WL = {
    "sh": "n=0; while [ $n -lt 90000000 ]; do n=$((n+1)); done",
    "pipe": "dd if=/dev/zero bs=4096 count=90000000 2>/dev/null | cat > /dev/null",
    "io": "while :; do dd if=/dev/zero of=/tmp/kp bs=4096 count=2000 "
          "2>/dev/null; rm -f /tmp/kp; done",
    "fork": "n=0; while [ $n -lt 90000000 ]; do /usr/bin/true; n=$((n+1)); done",
}


class Guest(object):
    def __init__(self, kernel, extra, name):
        env = dict(os.environ)
        env.update(KERNEL_ELF=kernel, WORK_DIR=os.path.join(D, "work"),
                   QEMU_SMP="1", QEMU_TIMEOUT="3000", QEMU_EXTRA=extra)
        os.makedirs(env["WORK_DIR"], exist_ok=True)
        self.log = open(os.path.join(D, name + ".console"), "w",
                        errors="replace")
        self.p = subprocess.Popen(["bash", RAMIMAGE, "-d"],
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, env=env,
                                  bufsize=0, start_new_session=True)
        os.set_blocking(self.p.stdout.fileno(), False)
        self.buf = ""

    def expect(self, s, t):
        end = time.time() + t
        while time.time() < end:
            try:
                c = self.p.stdout.read(4096)
            except BlockingIOError:
                c = None
            if c:
                c = c.decode(errors="replace")
                self.buf += c
                self.log.write(c)
                self.log.flush()
                if s in self.buf:
                    self.buf = self.buf[self.buf.index(s) + len(s):]
                    return
            else:
                if self.p.poll() is not None:
                    raise SystemExit("QEMU exited waiting for %r" % s)
                time.sleep(0.05)
        raise SystemExit("timeout waiting for %r" % s)

    def send(self, s):
        self.p.stdin.write((s + "\n").encode())
        self.p.stdin.flush()
        time.sleep(0.4)

    def start(self, wl, settle):
        self.expect("login:", 2400)
        self.send("root")
        self.expect("# ", 300)
        time.sleep(settle)      # rc's background work, first-boot rehash
        # Split so that the echo of the typed line does not match.
        self.send(WL.get(wl, wl) + " &")
        self.send("echo WL''-GO")
        self.expect("WL-GO", 600)

    def stop(self):
        try:
            os.killpg(self.p.pid, 9)
        except OSError:
            pass
        # timeout(1) puts QEMU in a process group of its own.
        subprocess.call(["pkill", "-9", "-f",
                         os.path.join(D, "work", "kernel.bin")])


# --- tb -------------------------------------------------------------------

ENDERS = re.compile(r"^(b|bl|br|blr|ret|eret|cbz|cbnz|tbz|tbnz|b\..*|isb|"
                    r"wfi|wfe|svc|hvc|smc|brk|sev|sevl)$")


def kernel_tables(kernel):
    insn = {}
    out = subprocess.run([XT + "objdump", "-d", "--no-show-raw-insn", kernel],
                         capture_output=True, text=True).stdout
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]{9,}):\s+(\S+)", line)
        if m:
            insn[int(m.group(1), 16)] = m.group(2)
    syms = []
    out = subprocess.run([XT + "nm", "-n", kernel], capture_output=True,
                         text=True).stdout
    for line in out.splitlines():
        f = line.split()
        if len(f) == 3 and f[1] in "tTwW":
            syms.append((int(f[0], 16), f[2]))
    return insn, syms


def tb_count(insn, start, nxt):
    """Instructions run by the TB at start, the next TB being at nxt."""
    n, a = 0, start
    while a in insn and n < 600:
        n += 1
        mn = insn[a]
        if ENDERS.match(mn):
            break
        # msr ends a TB only for some registers; the next start says.
        if a + 4 == nxt:
            break
        a += 4
    return n, a


def analyse_tb(kernel, logf):
    insn, syms = kernel_tables(kernel)
    sa = [a for a, _ in syms]

    def fname(pc):
        i = bisect.bisect_right(sa, pc) - 1
        return syms[i][1] if i >= 0 else "?"

    vec = [a for a, n in syms if n == "exception_vectors"][0]
    user_vecs = {vec + 0x400: "sync", vec + 0x480: "irq"}
    pat = re.compile(r"/([0-9a-f]{16})/")
    pcs = []
    with open(logf, errors="replace") as f:
        for line in f:
            if line.startswith("Trace"):
                m = pat.search(line)
                if m:
                    pcs.append(int(m.group(1), 16))
    print("TB executions: %d" % len(pcs))

    crossings, cur = [], None
    for i, pc in enumerate(pcs):
        if pc in user_vecs:
            if cur is not None:
                crossings.append(cur)
            cur = [user_vecs[pc], collections.Counter(), 0]
        if cur is None:
            continue
        nxt = pcs[i + 1] if i + 1 < len(pcs) else None
        n, a = tb_count(insn, pc, nxt)
        p = pc
        while p <= a:
            cur[1][fname(p)] += 1
            p += 4
        cur[2] += n

    def klass(c):
        if c[0] == "irq":
            return "irq"
        for f, k in (("copr_not_available_handler", "fpu"),
                     ("do_ipc", "ipc"), ("kernel_call", "kcall"),
                     ("abort_exception", "fault")):
            if f in c[1]:
                return k
        return "other"

    by = collections.defaultdict(list)
    for c in crossings:
        by[klass(c)].append(c)
    for k in sorted(by):
        lst = by[k]
        print("== %s: %d crossings, %.1f instructions each" %
              (k, len(lst), sum(c[2] for c in lst) / len(lst)))
        agg = collections.Counter()
        for c in lst:
            agg.update(c[1])
        for f, v in agg.most_common(40):
            print("   %-34s %7.1f" % (f, v / len(lst)))


def run_tb(a):
    logf = os.path.join(D, "exec-%s.log" % a.w)
    mon = os.path.join(D, "mon.sock")
    for f in (logf, mon):
        try:
            os.unlink(f)
        except OSError:
            pass
    extra = ("-icount shift=0 -d nochain -D %s "
             "-dfilter 0xffff000040000000+0x2000000 "
             "-monitor unix:%s,server,nowait" % (logf, mon))
    g = Guest(a.K, extra, "tb-" + a.w)

    def monitor(cmd):
        s = socket.socket(socket.AF_UNIX)
        s.connect(mon)
        time.sleep(0.2)
        s.recv(65536)
        s.sendall((cmd + "\n").encode())
        time.sleep(0.3)
        s.close()

    try:
        g.start(a.w, 20)
        time.sleep(10)
        monitor("log exec,nochain")
        time.sleep(a.t)
        monitor("log nochain")
    finally:
        g.stop()
    analyse_tb(a.K, logf)


# --- fp -------------------------------------------------------------------

GDB_FP = r'''
import gdb, os
gdb.execute("set pagination off")
gdb.execute("set confirm off")
gdb.execute("target remote :%(port)s")
gdb.execute("hbreak trap_handler if ((($x2 >> 26) & 0x3f) == 7)")
with open("%(out)s", "w") as f:
    for i in range(%(n)d):
        gdb.execute("continue", to_string=True)
        v = gdb.parse_and_eval("(struct proc *)$x0")
        f.write("%%s %%x\n" %% (v["p_name"].string(), int(v["p_reg"]["pc"])))
        f.flush()
gdb.execute("delete")
gdb.execute("detach")
'''


def binary_for(name):
    name = name.split("*")[0]
    for d in (os.path.join(DEST, "service"), os.path.join(DEST, "bin"),
              os.path.join(DEST, "usr/bin"), os.path.join(DEST, "sbin")):
        if os.path.isfile(os.path.join(d, name)):
            return os.path.join(d, name)
    for top in ("minix/servers", "minix/fs", "minix/drivers"):
        for root, _, files in os.walk(os.path.join(OBJ, top)):
            if name in files and os.path.basename(root) == name:
                return os.path.join(root, name)
    return None


def run_fp(a):
    out = os.path.join(D, "fp-%s.txt" % a.w)
    script = os.path.join(D, "fp.gdb.py")
    with open(script, "w") as f:
        f.write(GDB_FP % {"port": a.port, "out": out, "n": a.n})
    g = Guest(a.K, "-gdb tcp::%s" % a.port, "fp-" + a.w)
    try:
        g.start(a.w, 40)
        time.sleep(20)
        subprocess.call(["gdb-multiarch", "-nx", "-batch", "-x", script, a.K],
                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    finally:
        g.stop()
    rows = [l.split() for l in open(out) if l.strip()]
    print("%d traps" % len(rows))
    for who, n in collections.Counter(r[0] for r in rows).most_common():
        print("  %6d  %s" % (n, who))
    print("where:")
    for (who, pc), n in collections.Counter(
            (r[0], r[1]) for r in rows).most_common(20):
        b = binary_for(who)
        fn = ins = "?"
        if b:
            fn = subprocess.run([XT + "addr2line", "-f", "-e", b, "0x" + pc],
                                capture_output=True,
                                text=True).stdout.split("\n")[0]
            d = subprocess.run([XT + "objdump", "-d", "--no-show-raw-insn",
                                "--start-address=0x" + pc,
                                "--stop-address=0x%x" % (int(pc, 16) + 4), b],
                               capture_output=True, text=True).stdout
            ins = d.strip().splitlines()[-1].split(":", 1)[-1].strip()
        print("  %6d  %-12s %-28s %s" % (n, who, fn, ins))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=("tb", "fp"))
    ap.add_argument("-K", required=True, help="kernel ELF")
    ap.add_argument("-w", default="sh", help="workload")
    ap.add_argument("-t", type=float, default=3, help="tb: seconds logged")
    ap.add_argument("-n", type=int, default=500, help="fp: traps")
    ap.add_argument("--port", default="1236")
    a = ap.parse_args()
    os.makedirs(D, exist_ok=True)
    (run_tb if a.mode == "tb" else run_fp)(a)


if __name__ == "__main__":
    main()
