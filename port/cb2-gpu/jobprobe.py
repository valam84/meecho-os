#!/usr/bin/env python3
# Зонд заданий Mali-G52 CB2 из вендорского Linux, через ioctl panfrost.
#
# Зачем: вторая половина вехи 11.5 - MMU ускорителя и одно задание с
# проверкой результата процессором.  Прежде чем писать это в службе MEECHO,
# здесь, на системе, которую можно перезагрузить, проверяется на этом самом
# железе: что дескриптор задания WRITE_VALUE в той раскладке, что даёт Mesa
# (src/panfrost/genxml/v7.xml, MIT), исполняется; что цепочка таких заданий
# заливает буфер; и что именно panfrost кладёт в адресное пространство MMU
# (TRANSTAB, MEMATTR, TRANSCFG) - это эталон для своей MMU в службе.
#
# Пакетов не нужно: только python3, /dev/dri/renderD* и /dev/mem.  Блок на
# время зонда держится включённым (power/control = on), иначе panfrost через
# 50 мс гасит домен, и регистры MMU читать нельзя; прежнее значение
# возвращается.  Запуск под root:
#
#	python3 jobprobe.py > jobprobe.txt
#	python3 jobprobe.py fault > ref-fault.txt
#
# Режим fault - эталон линии MMU (73): цепочка, потом одно задание,
# пишущее туда, где ничего не отображено (FAULT_OFF от начала буфера: та
# же таблица последнего уровня, пустая запись), и цепочка снова - жив ли
# ускоритель после отказа.  Пока panfrost разбирается с отказом, зонд
# опрашивает регистры MMU, слота и адресного пространства и печатает
# каждое их изменение со временем от SUBMIT: что сделало железо и в каком
# порядке panfrost отвечал.  Ловится не всё - обработчик прерывания
# быстрее Python, - но порядок и конечные значения видны.  Потом новые
# строки dmesg.
#
# Раскладка дескриптора (v7.xml, слова по 32 бита):
#   заголовок, 32 байта: 0 состояние исключения (пишет GPU), 1 первая
#   незавершённая задача, 2-3 указатель отказа, 4: бит 0 is_64b, 7:1 тип,
#   8 барьер, 31:16 индекс; 5: зависимости 1 и 2; 6-7 следующее задание
#   (0 - конец цепочки);
#   WRITE_VALUE, с байта 32: адрес (64), тип значения (32; 6 - 32 бита,
#   7 - 64 бита), значение (64) с байта 48.

import fcntl, mmap, os, struct, sys, time

DEV = "/sys/bus/platform/devices/fde60000.gpu"
GPU_BASE = 0xfde60000

def IOC(d, nr, size):
    return (d << 30) | (size << 16) | (ord('d') << 8) | nr

IOW, IOWR = 1, 3
SUBMIT = IOC(IOW, 0x40, 40)
WAIT_BO = IOC(IOW, 0x41, 16)
CREATE_BO = IOC(IOWR, 0x42, 24)
MMAP_BO = IOC(IOWR, 0x43, 16)
GET_PARAM = IOC(IOWR, 0x44, 16)
GEM_CLOSE = IOC(IOW, 0x09, 8)

JOB_WRITE_VALUE = 2
WV_IMM32 = 6
JOB_SIZE = 64                   # заголовок 32 + нагрузка 24, выравнивание 64

# Отказ: буфер 16 КиБ занимает записи 0-3 таблицы последнего уровня,
# запись 16 пуста.  Не с начала страницы - видно, весь ли адрес
# запоминает FAULTADDRESS.  Служба MEECHO пишет туда же (0x2010040).
FAULT_OFF = 0x10040
FAULT_VALUE = 0xbadc0de5


def render_node():
    for n in sorted(os.listdir("/sys/class/drm")):
        if not n.startswith("renderD"):
            continue
        drv = os.path.realpath("/sys/class/drm/%s/device/driver" % n)
        if os.path.basename(drv) == "panfrost":
            return "/dev/dri/" + n
    raise SystemExit("no panfrost render node")


def ioctl(fd, req, fmt, *vals):
    buf = bytearray(struct.pack(fmt, *vals))
    fcntl.ioctl(fd, req, buf, True)
    return struct.unpack(fmt, buf)


def get_param(fd, p):
    return ioctl(fd, GET_PARAM, "<IIQ", p, 0, 0)[2]


def create_bo(fd, size):
    _, _, handle, _, va = ioctl(fd, CREATE_BO, "<IIIIQ", size, 0, 0, 0, 0)
    off = ioctl(fd, MMAP_BO, "<IIQ", handle, 0, 0)[2]
    m = mmap.mmap(fd, size, mmap.MAP_SHARED,
                  mmap.PROT_READ | mmap.PROT_WRITE, offset=off)
    return handle, va, m


def write_value_job(m, at, index, nxt, addr, value):
    """Дескриптор WRITE_VALUE по смещению at."""
    hdr = struct.pack("<IIQIIQ", 0, 0, 0,
                      1 | (JOB_WRITE_VALUE << 1) | (index << 16), 0, nxt)
    pay = struct.pack("<QIIQ", addr, WV_IMM32, 0, value)
    m[at:at + 32] = hdr
    m[at + 32:at + 56] = pay


def submit_wait(fd, handle, jc, during=None):
    """SUBMIT и WAIT_BO; during() - между ними, сразу после SUBMIT."""
    import ctypes
    harr = bytearray(struct.pack("<I", handle))
    c = (ctypes.c_uint32 * 1).from_buffer(harr)
    ioctl(fd, SUBMIT, "<QQIIQII", jc, 0, 0, 0, ctypes.addressof(c), 1, 0)
    t0 = time.monotonic()
    if during is not None:
        during()
    deadline = time.clock_gettime_ns(time.CLOCK_MONOTONIC) + 2 * 10**9
    try:
        ioctl(fd, WAIT_BO, "<IIq", handle, 0, deadline)
        ok = True
    except OSError as e:
        print("  WAIT_BO: %s" % e)
        ok = False
    return ok, time.monotonic() - t0


def gpu_regs():
    fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
    m = mmap.mmap(fd, 0x4000, mmap.MAP_SHARED, mmap.PROT_READ,
                  offset=GPU_BASE)
    os.close(fd)
    return m


def rd(m, off):
    return struct.unpack_from("<I", m, off)[0]


def dump_mmu(g, title):
    print("# %s" % title)
    for name, off in (("GPU_IRQ_RAWSTAT", 0x20), ("GPU_STATUS", 0x34),
                      ("JOB_IRQ_RAWSTAT", 0x1000), ("JOB_IRQ_JS_STATE", 0x1010),
                      ("MMU_IRQ_RAWSTAT", 0x2000), ("JM_CONFIG", 0xf00),
                      ("SHADER_CONFIG", 0xf04), ("TILER_CONFIG", 0xf08),
                      ("L2_MMU_CONFIG", 0xf0c)):
        print("  0x%04x %-22s 0x%08x" % (off, name, rd(g, off)))
    for js in range(3):
        b = 0x1800 + 0x80 * js
        print("  JS%d head %08x%08x tail %08x%08x affinity %08x%08x "
              "config %08x status %08x" % (js,
              rd(g, b + 4), rd(g, b), rd(g, b + 0xc), rd(g, b + 8),
              rd(g, b + 0x14), rd(g, b + 0x10), rd(g, b + 0x18),
              rd(g, b + 0x24)))
    for a in range(8):
        b = 0x2400 + 0x40 * a
        tt = (rd(g, b + 4) << 32) | rd(g, b)
        ma = (rd(g, b + 0xc) << 32) | rd(g, b + 8)
        tc = (rd(g, b + 0x34) << 32) | rd(g, b + 0x30)
        st = rd(g, b + 0x28)
        fs = rd(g, b + 0x1c)
        if tt or ma or tc or st or fs:
            print("  AS%d transtab 0x%016x memattr 0x%016x transcfg "
                  "0x%016x status 0x%x faultstatus 0x%x faultaddr "
                  "0x%08x%08x" % (a, tt, ma, tc, st, fs, rd(g, b + 0x24),
                  rd(g, b + 0x20)))


def main():
    pc = DEV + "/power/control"
    orig = open(pc).read().strip()
    open(pc, "w").write("on")
    time.sleep(0.2)
    rc = 1
    try:
        rc = probe_fault() if sys.argv[1:] == ["fault"] else probe()
    finally:
        open(pc, "w").write(orig)
        print("\npower/control restored: %s" % open(pc).read().strip())
    return rc


def probe():
    node = render_node()
    fd = os.open(node, os.O_RDWR)
    print("render node %s" % node)
    print("GPU_PROD_ID 0x%x, SHADER_PRESENT 0x%x" % (get_param(fd, 0),
          get_param(fd, 2)))
    g = gpu_regs()
    dump_mmu(g, "before any job")

    handle, va, m = create_bo(fd, 0x4000)
    print("BO handle %d, GPU VA 0x%x" % (handle, va))
    failures = 0

    # Одно задание: 32-битное значение в буфер.
    target = 0x1000
    m[target:target + 8] = struct.pack("<II", 0xdeadbeef, 0xdeadbeef)
    write_value_job(m, 0, 1, 0, va + target, 0xc0ffee42)
    ok, dt = submit_wait(fd, handle, va)
    got = struct.unpack_from("<II", m, target)
    status = struct.unpack_from("<I", m, 0)[0]
    print("\none WRITE_VALUE: done=%s in %.3f ms, target 0x%08x 0x%08x "
          "(want 0xc0ffee42 0xdeadbeef), header status 0x%x" % (
          ok, dt * 1000, got[0], got[1], status))
    if not ok or got != (0xc0ffee42, 0xdeadbeef):
        failures += 1
    dump_mmu(g, "after one job, the file still open")

    # Цепочка: 64 задания, каждое пишет своё слово - заливка буфера.
    n = 64
    base = 0x2000
    for i in range(n):
        m[base + 4 * i:base + 4 * i + 4] = struct.pack("<I", 0)
    for i in range(n):
        nxt = va + JOB_SIZE * (i + 1) if i + 1 < n else 0
        write_value_job(m, JOB_SIZE * i, i + 1, nxt, va + base + 4 * i,
                        0x5a000000 | i)
    ok, dt = submit_wait(fd, handle, va)
    words = struct.unpack_from("<%dI" % n, m, base)
    bad = [i for i in range(n) if words[i] != (0x5a000000 | i)]
    st = [struct.unpack_from("<I", m, JOB_SIZE * i)[0] for i in range(n)]
    print("\nchain of %d: done=%s in %.3f ms, %d wrong word(s)%s; header "
          "status first 0x%x last 0x%x" % (n, ok, dt * 1000, len(bad),
          (" at %s" % bad[:8]) if bad else "", st[0], st[-1]))
    if not ok or bad:
        failures += 1
    dump_mmu(g, "after the chain")

    # Корень таблиц: ОЗУ через /dev/mem ядро может не отдать (STRICT_DEVMEM).
    for a in range(8):
        b = 0x2400 + 0x40 * a
        tt = (rd(g, b + 4) << 32) | rd(g, b)
        if tt == 0:
            continue
        pa = tt & 0xfffffffffffff000
        try:
            f = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
            pm = mmap.mmap(f, 0x1000, mmap.MAP_SHARED, mmap.PROT_READ,
                           offset=pa)
            os.close(f)
            ent = [struct.unpack_from("<Q", pm, 8 * i)[0] for i in range(512)]
            print("\nAS%d table root 0x%x, non-zero entries:" % (a, pa))
            for i, e in enumerate(ent):
                if e:
                    print("  [%3d] 0x%016x" % (i, e))
        except OSError as e:
            print("\nAS%d table root 0x%x: not readable (%s)" % (a, pa, e))

    m.close()
    ioctl(fd, GEM_CLOSE, "<II", handle, 0)
    os.close(fd)

    # Что panfrost сказал о заданиях (отказы, сбросы) - в журнал ядра.
    log = os.popen("dmesg | grep -i panfrost | tail -5").read()
    print("\ndmesg (panfrost, last lines):\n" + log.rstrip())
    print("\n%s" % ("ALL JOBS OK" if failures == 0 else
                    "%d CHECK(S) FAILED" % failures))
    return 0 if failures == 0 else 1


def chain(fd, handle, va, m, title, n=64, base=0x2000):
    """Цепочка из n WRITE_VALUE, каждое - своё слово; True, если всё так."""
    for i in range(n):
        m[base + 4 * i:base + 4 * i + 4] = struct.pack("<I", 0)
    for i in range(n):
        nxt = va + JOB_SIZE * (i + 1) if i + 1 < n else 0
        write_value_job(m, JOB_SIZE * i, i + 1, nxt, va + base + 4 * i,
                        0x5a000000 | i)
    ok, dt = submit_wait(fd, handle, va)
    words = struct.unpack_from("<%dI" % n, m, base)
    bad = [i for i in range(n) if words[i] != (0x5a000000 | i)]
    st = [struct.unpack_from("<I", m, JOB_SIZE * i)[0] for i in range(n)]
    print("\n%s: chain of %d: done=%s in %.3f ms, %d wrong word(s)%s; "
          "header status first 0x%x last 0x%x" % (title, n, ok, dt * 1000,
          len(bad), (" at %s" % bad[:8]) if bad else "", st[0], st[-1]))
    return ok and not bad and st[0] == 1 and st[-1] == 1


# Что опрашивается, пока задание отказывает: (имя, смещение или функция
# номера адресного пространства).
def timeline_regs(a):
    b = 0x2400 + 0x40 * a
    return (("MMU_RAW", 0x2000), ("MMU_MASK", 0x2008),
            ("JOB_RAW", 0x1000), ("JOB_MASK", 0x1008),
            ("JS_STATE", 0x1010), ("JS1_STATUS", 0x1824 + 0x80),
            ("JS1_HEAD", 0x1800 + 0x80), ("AS_STATUS", b + 0x28),
            ("AS_FSTAT", b + 0x1c), ("AS_FADDR", b + 0x20),
            ("AS_TTAB", b + 0x00), ("AS_MATTR", b + 0x08),
            ("GPU_RAW", 0x20))


def timeline(g, a, secs, out):
    """Каждое изменение регистров за secs секунд, со временем."""
    regs = timeline_regs(a)
    offs = [o for _, o in regs]
    t0 = time.monotonic()
    last = None
    polls = 0
    while True:
        t = time.monotonic() - t0
        if t > secs or len(out) >= 400:
            break
        cur = tuple(rd(g, o) for o in offs)
        polls += 1
        if cur != last:
            out.append((t, cur))
            last = cur
    out.append((time.monotonic() - t0, None, polls))


def fault_status(fs):
    acc = ("ATOMIC", "EXECUTE", "READ", "WRITE")[(fs >> 8) & 3]
    return ("exception 0x%02x, access %s, %s, source 0x%x" % (fs & 0xff,
            acc, "decoder" if fs & (1 << 10) else "slave", fs >> 16))


def probe_fault():
    node = render_node()
    fd = os.open(node, os.O_RDWR)
    print("render node %s, mode fault" % node)
    g = gpu_regs()
    handle, va, m = create_bo(fd, 0x4000)
    print("BO handle %d, GPU VA 0x%x (16 KiB: last-level entries 0-3)" % (
          handle, va))
    failures = 0

    # Эталонная цепочка: и проверка, и номер адресного пространства.
    if not chain(fd, handle, va, m, "before the fault"):
        failures += 1
    a = rd(g, 0x1818 + 0x80) & 0xf
    print("JS1 CONFIG 0x%08x: address space %d" % (rd(g, 0x1818 + 0x80), a))
    dump_mmu(g, "before the fault")

    dmesg0 = os.popen("dmesg").read().splitlines()

    # Одно задание туда, где ничего нет.
    fva = va + FAULT_OFF
    m[0x3000:0x3008] = struct.pack("<II", 0xdeadbeef, 0xdeadbeef)
    write_value_job(m, 0, 1, 0, fva, FAULT_VALUE)
    tl = []
    ok, dt = submit_wait(fd, handle, va,
                         during=lambda: timeline(g, a, 1.0, tl))
    status = struct.unpack_from("<IIQ", m, 0)
    print("\nfault job: WRITE_VALUE 0x%08x to unmapped GPU VA 0x%x; "
          "done=%s after %.3f ms (wait after a 1 s watch); header status "
          "0x%x, first incomplete 0x%x, fault pointer 0x%x" % (
          FAULT_VALUE, fva, ok, dt * 1000, status[0], status[1], status[2]))

    names = [nm for nm, _ in timeline_regs(a)]
    print("\n# registers while it faults, every change (t from SUBMIT):")
    print("  %9s  %s" % ("t, us", " ".join("%-10s" % nm for nm in names)))
    for e in tl:
        if e[1] is None:
            print("  (%d polls in %.3f s)" % (e[2], e[0]))
            continue
        print("  %9.0f  %s" % (e[0] * 1e6,
              " ".join("0x%08x" % v for v in e[1])))
    last = [e for e in tl if e[1] is not None][-1][1]
    fs = last[names.index("AS_FSTAT")]
    print("\nlast AS%d FAULTSTATUS 0x%08x: %s; FAULTADDRESS 0x%08x%08x "
          "(asked for 0x%x)" % (a, fs, fault_status(fs),
          rd(g, 0x2400 + 0x40 * a + 0x24), rd(g, 0x2400 + 0x40 * a + 0x20),
          fva))
    print("JS1 STATUS 0x%02x" % rd(g, 0x1824 + 0x80))
    dump_mmu(g, "after the fault, before the next job")

    dmesg1 = os.popen("dmesg").read().splitlines()
    new = dmesg1[len(dmesg0):] if dmesg1[:len(dmesg0)] == dmesg0 else \
        [l for l in dmesg1 if l not in set(dmesg0)]
    print("\ndmesg, new lines:\n" + "\n".join(new))
    if not any("fault" in l.lower() for l in new):
        print("NO FAULT REPORTED BY PANFROST")
        failures += 1

    # Жив ли ускоритель: та же цепочка в том же адресном пространстве.
    if not chain(fd, handle, va, m, "after the fault"):
        failures += 1
    dump_mmu(g, "after the chain that follows the fault")

    m.close()
    ioctl(fd, GEM_CLOSE, "<II", handle, 0)
    os.close(fd)
    print("\n%s" % ("FAULT REPORTED, CHAINS OK" if failures == 0 else
                    "%d CHECK(S) FAILED" % failures))
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
