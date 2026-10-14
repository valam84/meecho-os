#!/usr/bin/env python3
"""
Worst-case kernel stack depth, from GCC's own numbers (KERNEL-AUDIT #9).

port/stack-build.sh rebuilds the kernel and the four libraries linked into it
with -fstack-usage -fcallgraph-info=su. Each object then has a .ci file: VCG
nodes carrying the frame size of every function GCC compiled, and edges for
every call it saw. This script joins them into one graph, resolves the
indirect calls by hand (the list below says where each set comes from), and
walks it from the places the assembly enters C.

What the numbers are and are not:

- A frame is what the function's prologue subtracts, as GCC reports it. A
  call itself costs nothing more on AArch64: the return address is in x30,
  saved in the callee's frame if at all.
- Functions GCC did not compile have no node with a size: the assembly
  (usercopy, spinlocks, fpu_asm, memcpy and friends in libminc). They are
  listed, and counted as zero; each is a leaf that touches sp not at all or
  by a few words, which the listing lets one check.
- Recursion is reported, not followed: a cycle has no worst case without
  knowing its bound.
- Indirect calls not in the table are reported. An unresolved one on a path
  that matters means the number is a lower bound.

Usage: python3 stack-depth.py [OBJROOT]      (default ~/obj-evbarm64/minix)
"""

import glob
import os
import re
import sys

ARGS = [a for a in sys.argv[1:] if not a.startswith("-")]
OBJ = ARGS[0] if ARGS else os.path.expanduser("~/obj-evbarm64/minix")
SRC = os.path.expanduser("~/minix-src/minix/kernel")

DIRS = ["kernel", "lib/libminc", "lib/libsys", "lib/libtimers",
        "lib/libexec"]

K_STACK_SIZE = 8192             # K_STACK_SIZE, archconst.h
STACK_TOP_RESERVED = 16         # AARCH64_STACK_TOP_RESERVED
FRAME_SIZE = 34 * 8             # trap.h, the exception frame
OVERFLOW_STACK_SIZE = 8192      # K_OVERFLOW_STACK_SIZE
BOOT_STACK_SIZE = 16384         # head.S, boot_stack

node_re = re.compile(r'node: \{ title: "([^"]+)" label: "([^"]*)"')
edge_re = re.compile(r'edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"'
                     r'(?: label: "([^"]*)")?')
size_re = re.compile(r'\\n(\d+) bytes \(([a-z,]+)\)')

frame = {}      # function -> bytes
kind = {}       # function -> static / dynamic / dynamic,bounded
calls = {}      # function -> set of callees
indirect = {}   # function -> [call sites]
where = {}      # function -> object it came from

# A name defined in more than one place (kputc: the kernel, libsys and
# libminc; panic: the kernel and libsys; printf: libsys and libminc) is the
# one the linker takes, which is the first in link order: the kernel's own
# objects, then -ltimers -lsys -lexec, then libminc. DIRS is in that order,
# and a later definition is dropped whole - its frame and its edges. Merging
# them instead once had the kernel's kputc "calling" libsys's sys_diagctl.
owner = {}      # function -> the directory whose definition wins
for d in DIRS:
    for ci in glob.glob(os.path.join(OBJ, d, "*.ci")):
        obj = os.path.relpath(ci, OBJ)
        with open(ci, errors="replace") as f:
            text = f.read()
        mine = set()
        for title, label in node_re.findall(text):
            m = size_re.search(label)
            if not m or owner.get(title, d) != d:
                continue
            owner[title] = d
            mine.add(title)
            frame[title] = int(m.group(1))
            kind[title] = m.group(2)
            where[title] = obj
        for src, dst, site in edge_re.findall(text):
            if src not in mine:
                continue
            if dst == "__indirect_call":
                indirect.setdefault(src, []).append(site)
            else:
                calls.setdefault(src, set()).add(dst)


def named(short):
    """A function by its plain name: exact title, or a static one's
    'file:name' title."""
    if short in frame or short in calls:
        return [short]
    return [t for t in frame if t.endswith(":" + short)]


# --- indirect calls, resolved by hand --------------------------------------

# kernel_call_dispatch: call_vec[], filled by map() in system.c.
with open(os.path.join(SRC, "system.c")) as f:
    call_vec = sorted(set(re.findall(r'map\(\s*\w+\s*,\s*(\w+)\s*\)',
                                     f.read())))

RESOLVE = {
    # interrupt.c: hook->handler. put_irq_handler() is called with exactly
    # these (clock.c through generic_timer.c, and do_irqctl.c); profile.c's
    # is not built.
    "irq_handle": ["timer_int_handler", "generic_handler"],
    # system.c: call_vec[call_nr].
    "kernel_call_dispatch": call_vec,
    # subr_prf.c: put(), which is kputc (-Dputchar=kputc) or sputchar
    # for snprintf.
    "kdoprnt": ["kputc", "sputchar"],
    "kprintn": ["kputc", "sputchar"],
    # libtimers: tmr_func. set_kernel_timer() is called with these two.
    "tmrs_exptimers": ["minix_shutdown", "cause_alarm"],
    # libsys fdt.c: the walker's callback. Resolved per caller below, not
    # here: every callback the kernel passes is a static function in the
    # caller's own file, and joining them all to fdt_walk would let psci's
    # probe "call" the memory scan of pre_init.c.
    "fdt_walk": [],
    # libexec: the exec_info callbacks protect.c sets.
    "libexec_load_elf": ["libexec_copy_memcpy", "libexec_clear_memset",
                         "libexec_pg_alloc"],
}

unresolved_names = set()
for caller, sites in indirect.items():
    short = caller.split(":")[-1]
    targets = RESOLVE.get(short)
    if targets is None:
        unresolved_names.add(caller)
        continue
    for t in targets:
        found = named(t)
        if not found:
            print(f"warning: {t} (target of {short}) has no node")
        for n in found:
            calls.setdefault(caller, set()).add(n)


# fdt_walk, per caller: a copy of the walker's node for each object that
# calls it, whose indirect call reaches the static functions of that object
# with the walker's callback signature. The candidates are named here - the
# ones passed to fdt_walk() in the kernel's sources.
FDT_CALLBACKS = ["find_gic", "find_cpus", "find_timer", "scan_chosen",
                 "scan_alias", "scan_uart", "scan_reserved", "scan_node",
                 "find_psci"]
fdt_cb = {}
for cb in FDT_CALLBACKS:
    for n in named(cb):
        fdt_cb.setdefault(where[n], []).append(n)
for caller in list(calls):
    if "fdt_walk" not in calls[caller] or caller not in where:
        continue
    cbs = fdt_cb.get(where[caller], [])
    if not cbs:
        continue
    v = f"fdt_walk[{where[caller]}]"
    frame[v], kind[v], where[v] = frame["fdt_walk"], kind["fdt_walk"], \
        where["fdt_walk"]
    calls[v] = set(calls.get("fdt_walk", ())) | set(cbs)
    calls[caller].discard("fdt_walk")
    calls[caller].add(v)



# --- the walk ---------------------------------------------------------------

# Two questions, not one. A path that enters panic() or a failed assert() is
# a machine already dying, and the deepest of those is an error inside the
# report of an error inside the report of another: assert -> printf -> ...
# -> panic -> proc_stacktrace -> printf ... - a number with no meaning. So:
#
#   working  - the deepest path that never enters DEATH;
#   dying    - the deepest working path to a call into DEATH, plus the depth
#              of that one report, inside which DEATH is not entered again.
#
# The guard page is what covers anything deeper than "dying".
DEATH = {"panic", "__assert13"}

missing = set()
unresolved_hit = set()


def cyclic_nodes():
    """Functions that are part of some cycle of the call graph (a strongly
    connected component with more than one member, or a self-call). Tarjan's
    algorithm, iteratively - the graph is deep enough to exhaust Python's
    recursion limit."""
    index, low, on, st, out = {}, {}, set(), [], set()
    counter = [0]
    for start in list(calls):
        if start in index:
            continue
        work = [(start, iter(calls.get(start, ())))]
        index[start] = low[start] = counter[0]
        counter[0] += 1
        st.append(start)
        on.add(start)
        while work:
            v, it = work[-1]
            advanced = False
            for w in it:
                if w not in index:
                    index[w] = low[w] = counter[0]
                    counter[0] += 1
                    st.append(w)
                    on.add(w)
                    work.append((w, iter(calls.get(w, ()))))
                    advanced = True
                    break
                if w in on:
                    low[v] = min(low[v], index[w])
            if advanced:
                continue
            work.pop()
            if work:
                low[work[-1][0]] = min(low[work[-1][0]], low[v])
            if low[v] == index[v]:
                comp = []
                while True:
                    w = st.pop()
                    on.discard(w)
                    comp.append(w)
                    if w == v:
                        break
                if len(comp) > 1 or v in calls.get(v, ()):
                    out.update(comp)
    return out


CYCLIC = cyclic_nodes()


class Walk:
    """Deepest stack from a function, never entering the blocked set below
    the root, and never calling a function that is already on the path.

    The last rule is how recursion is handled: printf -> kputc ->
    send_diag_sig -> ... -> virtual_copy_f -> printf is a real edge (an error
    message while delivering the diagnostics signal), and following it once
    more would be following it forever. So a chain has every function at
    most once. To make that exact rather than approximate, what is remembered
    is keyed on the function and on which of the functions that take part in
    cycles are already on the path: only those can change the answer. An
    earlier version keyed on the function alone and either let printf appear
    twice in one chain or, cut the other way, made printf a leaf everywhere."""

    def __init__(self, blocked):
        self.blocked = blocked
        self.memo = {}

    def depth(self, fn, on_path=frozenset(), root=True):
        """(bytes, path, None)."""
        if not root and fn in self.blocked:
            return (0, [], None)
        if fn in on_path:
            return (0, [], None)
        key = (fn, on_path)
        if key in self.memo:
            return self.memo[key]
        if fn not in frame:
            missing.add(fn)
        if fn in unresolved_names:
            unresolved_hit.add(fn)
        below = on_path | {fn} if fn in CYCLIC else on_path
        own = frame.get(fn, 0)
        best, best_path = 0, []
        for c in calls.get(fn, ()):
            d, p, _ = self.depth(c, below, False)
            if d > best:
                best, best_path = d, p
        r = (own + best, [(fn, own)] + best_path, None)
        self.memo[key] = r
        return r

    def depth_to(self, fn, targets, on_path=frozenset()):
        """Deepest stack at the moment a descendant of fn calls one of
        targets - the callers' frames only - as ((bytes, path, target),
        None), or (None, None)."""
        if fn in on_path:
            return (None, None)
        key = ("to", fn, on_path, frozenset(targets))
        if key in self.memo:
            return self.memo[key]
        below = on_path | {fn} if fn in CYCLIC else on_path
        best = None
        for c in calls.get(fn, ()):
            if c in targets:
                r = (0, [], c)
            elif c in self.blocked:
                continue
            else:
                r, _ = self.depth_to(c, targets, below)
            if r is not None and (best is None or r[0] > best[0]):
                best = r
        if best is not None:
            own = frame.get(fn, 0)
            best = (own + best[0], [(fn, own)] + best[1], best[2])
        self.memo[key] = (best, None)
        return (best, None)



working = Walk(DEATH)
# Inside a report, a second one is not followed: re-entering panic is cut as
# recursion, a failed assert inside it is blocked.
report = Walk({"__assert13"})


def show(title, total, path, budget):
    print(f"\n{title}: {total} bytes of {budget} ({budget - total} left)")
    for fn, n in path:
        k = kind.get(fn, "no size: assembly or not built")
        print(f"  {n:5d}  {fn}  [{k}]")


def root(name):
    r = named(name)
    if not r:
        sys.exit(f"no node for {name}: was port/stack-build.sh run?")
    return r[0]


def both(title, fn, budget, before=0, before_path=()):
    """Print the working and the dying depth from fn, with before bytes
    already on the stack."""
    d, p, _ = working.depth(fn)
    show(f"{title}, working", before + d, list(before_path) + p, budget)
    r, _ = working.depth_to(fn, DEATH)
    if r is not None:
        rd, rp, _ = report.depth(r[2])
        show(f"{title}, dying", before + r[0] + rd,
             list(before_path) + r[1] + rp, budget)
        return max(before + d, before + r[0] + rd), before + d
    return before + d, before + d


avail = K_STACK_SIZE - STACK_TOP_RESERVED
summary = []

# 1. Entry from EL0: the frame is in p_reg; the kernel stack starts at its
#    top with trap_handler, and switch_to_user after it returns.
summary.append(("EL0 entry: trap_handler",
                both("EL0 entry: trap_handler", root("trap_handler"), avail),
                avail))
summary.append(("EL0 entry: switch_to_user",
                both("EL0 entry: switch_to_user", root("switch_to_user"),
                     avail), avail))

# 2. Idle wake-up: the frame goes on top of switch_to_user -> ... -> halt_cpu,
#    then trap_handler.
r, _ = working.depth_to(root("switch_to_user"), {"halt_cpu"})
if r is not None:
    pre = r[0] + FRAME_SIZE
    summary.append(("idle wake-up",
                    both("idle wake-up", root("trap_handler"), avail, pre,
                         r[1] + [("<exception frame>", FRAME_SIZE)]), avail))

# 3. A fault inside the kernel on a user copy: frame on top of wherever the
#    copy was, then trap_handler. copy_msg_* are the kernel's only
#    deliberately faulting accesses (usercopy.S).
copies = {"copy_msg_from_user", "copy_msg_to_user"}
r, _ = working.depth_to(root("trap_handler"), copies)
if r is not None:
    pre = r[0] + FRAME_SIZE
    summary.append(("fault in a user copy",
                    both("fault in a user copy", root("trap_handler"), avail,
                         pre, r[1] + [(r[2], 0),
                                      ("<exception frame>", FRAME_SIZE)]),
                    avail))

# 4. Secondary boot: smp_ap_boot runs on the core's own kernel stack.
summary.append(("secondary boot",
                both("secondary boot", root("smp_ap_boot"), avail), avail))

# 5. The overflow report, on the 8 KiB emergency stack below a frame.
summary.append(("overflow report (8 KiB stack)",
                both("overflow report", root("trap_stack_overflow"),
                     OVERFLOW_STACK_SIZE, FRAME_SIZE,
                     [("<exception frame>", FRAME_SIZE)]),
                OVERFLOW_STACK_SIZE))

# 6. Boot, on the 16 KiB boot stack.
summary.append(("boot (16 KiB stack)",
                both("boot", root("pre_init"), BOOT_STACK_SIZE),
                BOOT_STACK_SIZE))

print("\nsummary (bytes: working / dying, of the stack):")
for name, (dying, work), budget in summary:
    print(f"  {name:32s} {work:5d} / {dying:5d}  of {budget}")

# Every kernel call on its own: EL0 entry -> kernel_call -> dispatch -> do_X.
prefix = sum(frame.get(f, 0) for f in (root("trap_handler"),
                                       root("kernel_call"))) + \
    frame.get(named("kernel_call_dispatch")[0], 0)
print(f"\nkernel calls, working depth, deepest first (entry prefix {prefix}"
      f" bytes, budget {avail}):")
rows = []
for h in call_vec:
    for n in named(h):
        d, p, _ = working.depth(n)
        rows.append((prefix + d, n, p))
for tot, n, p in sorted(rows, key=lambda r: -r[0])[:12]:
    tail = " -> ".join(f.split(":")[-1] for f, _ in p[1:6])
    print(f"  {tot:5d}  {n:24s} own {frame.get(n, 0):5d}  {tail}")

# The largest frames among functions the kernel can reach at all.
reach = set()
todo = [root("trap_handler"), root("switch_to_user"), root("smp_ap_boot"),
        root("trap_stack_overflow"), root("pre_init")]
while todo:
    f = todo.pop()
    if f in reach:
        continue
    reach.add(f)
    todo.extend(calls.get(f, ()))
print("\nlargest frames reachable from the kernel's entries:")
for fn in sorted((f for f in reach if f in frame),
                 key=lambda f: -frame[f])[:12]:
    print(f"  {frame[fn]:5d}  {fn}  [{kind[fn]}]  {where[fn]}")

dyn = [f for f in reach if f in frame and kind[f] != "static"]
if dyn:
    print("\nframes that are not static (alloca or VLA):")
    for f in dyn:
        print(f"  {frame[f]:5d}  {f}  [{kind[f]}]")

if unresolved_hit:
    print("\nunresolved indirect calls on these paths:")
    for u in sorted(unresolved_hit):
        print(f"  {u}: {', '.join(indirect[u])}")
print(f"\nrecursion cut: {len(CYCLIC)} functions on cycles (all through "
      f"panic, assert or printf; -v lists them)")
if "-v" in sys.argv:
    for c in sorted(CYCLIC):
        print("  " + c)
print("\nno size (assembly, or not in these objects), counted as 0:")
print("  " + " ".join(sorted(m for m in missing if m in reach or True)))
