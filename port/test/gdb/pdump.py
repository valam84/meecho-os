# gdb -batch -x pdump.py: dump the kernel process table of a stopped guest.
import gdb

RTS = [(0x01, "FREE"), (0x02, "PSTOP"), (0x04, "SEND"), (0x08, "RECV"),
       (0x10, "SIGNALED"), (0x20, "SIGPEND"), (0x40, "P_STOP"),
       (0x80, "NOPRIV"), (0x100, "NOEP"), (0x200, "VMINH"),
       (0x400, "PGFAULT"), (0x800, "VMREQ"), (0x1000, "VMREQTGT"),
       (0x4000, "PREEMPT"), (0x8000, "NOQUANT"), (0x10000, "BOOTINH")]


def flags(v):
    return "|".join(n for b, n in RTS if v & b) or "-"


def ep(e):
    e = int(e)
    if e == 0x7ffe or e == -1:
        return "ANY" if e == 0x7ffe else "?"
    return str(e)


def main():
    gdb.execute("set pagination off")
    procs = gdb.parse_and_eval("proc")
    n = int(procs.type.sizeof // procs[0].type.sizeof)
    names = {}
    for i in range(n):
        p = procs[i]
        names[int(p["p_endpoint"])] = p["p_name"].string()
    for i in range(n):
        p = procs[i]
        rts = int(p["p_rts_flags"])
        if rts & 1:
            continue
        gf = int(p["p_getfrom_e"])
        st = int(p["p_sendto_e"])
        line = "%3d %-12s ep=%-6d rts=%-22s misc=%#x pc=%#x u=%d s=%d" % (
            i, p["p_name"].string(), int(p["p_endpoint"]), flags(rts),
            int(p["p_misc_flags"]), int(p["p_reg"]["pc"]),
            int(p["p_user_time"]), int(p["p_sys_time"]))
        if rts & 0x08:
            line += " from=%s" % names.get(gf, ep(gf))
        if rts & 0x04:
            line += " to=%s" % names.get(st, ep(st))
        if rts & 0x1800:
            try:
                vr = p["p_vmrequest"]
                line += " vmreq{type=%d req=%d target=%d start=%#x len=%#x w=%d res=%d}" % (
                    int(vr["type"]), int(vr["req_type"]), int(vr["target"]),
                    int(vr["params"]["check"]["start"]),
                    int(vr["params"]["check"]["length"]),
                    int(vr["params"]["check"]["writeflag"]),
                    int(vr["vmresult"]))
            except Exception as e:
                line += " vmreq?"
        print(line)


main()
