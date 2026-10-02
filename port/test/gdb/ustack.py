# gdb -x ustack.py -ex "python ustack(SLOT)": frame-pointer walk of a user
# process, reading its memory through its own page table and the kernel's
# linear map of RAM.
import gdb
import struct

OFF = 0xffff000000000000
inf = None


def rd(pa, n):
    global inf
    if inf is None:
        inf = gdb.selected_inferior()
    return bytes(inf.read_memory(OFF + pa, n))


def q(pa):
    return struct.unpack("<Q", rd(pa, 8))[0]


def v2p(root, va):
    t = root
    for lvl, sh in enumerate((39, 30, 21, 12)):
        d = q(t + ((va >> sh) & 0x1ff) * 8)
        if not d & 1:
            return None
        if lvl < 3 and not d & 2:          # block descriptor
            mask = (1 << sh) - 1
            return (d & 0x0000fffffffff000 & ~mask) | (va & mask)
        t = d & 0x0000fffffffff000
    return t | (va & 0xfff)


def uq(root, va):
    pa = v2p(root, va)
    return None if pa is None else q(pa)


def ustack(slot, depth=24):
    p = gdb.parse_and_eval("proc")[slot]
    root = int(p["p_seg"]["p_ttbr"]) & 0x0000fffffffffffe
    r = p["p_reg"]
    print("%s pc=%#x lr=%#x sp=%#x fp=%#x" % (p["p_name"].string(),
          int(r["pc"]), int(r["lr"]), int(r["sp"]), int(r["fp"])))
    fp = int(r["fp"])
    for _ in range(depth):
        if fp == 0:
            break
        nfp = uq(root, fp)
        ret = uq(root, fp + 8)
        if nfp is None or ret is None:
            print("  fp %#x unmapped" % fp)
            break
        print("  ret %#x" % ret)
        fp = nfp


def uread(slot, va, n):
    """n 64-bit words of a process's memory."""
    p = gdb.parse_and_eval("proc")[slot]
    root = int(p["p_seg"]["p_ttbr"]) & 0x0000fffffffffffe
    return [uq(root, va + 8 * i) for i in range(n)]


def scan(slot, n=600, lo=0x400000, hi=0x500000):
    p = gdb.parse_and_eval("proc")[slot]
    root = int(p["p_seg"]["p_ttbr"]) & 0x0000fffffffffffe
    sp = int(p["p_reg"]["sp"])
    out = ["%#x" % int(p["p_reg"]["lr"])]
    for i in range(n):
        v = uq(root, sp + 8 * i)
        if v is None:
            break
        if lo <= v < hi:
            out.append("%#x" % v)
    open("scan-%d.txt" % slot, "w").write(" ".join(out) + "\n")
