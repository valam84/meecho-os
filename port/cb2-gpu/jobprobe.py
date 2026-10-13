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
#	python3 jobprobe.py api > ref-api.txt
#	python3 jobprobe.py two > ref-two.txt
#
# Режим api (веха 11.6) - эталон интерфейса, который служба mali отдаёт
# клиентам: что panfrost отвечает на каждый вызов, который служба
# реализует, с теми же аргументами, что гоняет хостовый стенд
# (port/test/mali/drmtest.c) и программа MEECHO (port/test/mali/pftest.c,
# тот же вывод строка в строку): версия, возможности, все параметры,
# места буферов по порядку создания, дескрипторы, ошибки, ожидания со
# сроками, объекты синхронизации, отказ задания (что возвращают WAIT_BO и
# ожидание объекта, если задание упало), куча.
#
# Режим two - два клиента (два открытия узла, два процесса) гоняют
# цепочки одновременно, один на середине ставит задание с отказом: что
# видит каждый, в каких адресных пространствах идут их задания.
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
GET_BO_OFFSET = IOC(IOWR, 0x45, 16)
MADVISE = IOC(IOWR, 0x48, 12)
GEM_CLOSE = IOC(IOW, 0x09, 8)
VERSION = IOC(IOWR, 0x00, 64)
GET_CAP = IOC(IOWR, 0x0c, 16)
SYNCOBJ_CREATE = IOC(IOWR, 0xbf, 8)
SYNCOBJ_DESTROY = IOC(IOWR, 0xc0, 8)
SYNCOBJ_WAIT = IOC(IOWR, 0xc3, 40)
SYNCOBJ_RESET = IOC(IOWR, 0xc4, 16)
SYNCOBJ_SIGNAL = IOC(IOWR, 0xc5, 16)

BO_NOEXEC, BO_HEAP = 1, 2
WAIT_ALL, WAIT_FOR_SUBMIT = 1, 2
REQ_FS = 1

JOB_WRITE_VALUE = 2
WV_IMM32 = 6
JOB_SIZE = 64                   # заголовок 32 + нагрузка 24, выравнивание 64

# Отказ: буфер 16 КиБ занимает записи 0-3 таблицы последнего уровня,
# запись 16 пуста.  Не с начала страницы - видно, весь ли адрес
# запоминает FAULTADDRESS.  Служба MEECHO пишет туда же (0x2010040).
FAULT_OFF = 0x10040
FAULT_VALUE = 0xbadc0de5

# Режим api: в его адресном пространстве много буферов, и FAULT_OFF от
# первого попадает в буфер 100 КиБ (0x2008000-0x2021000).  Пусто
# наверняка - 0x2100040: та же таблица последнего уровня (0x2000000-
# 0x21fffff), между буфером 100 КиБ и буфером 3 МиБ (с 0x2200000).
API_FAULT_OFF = 0x100040


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


# ---- режимы heap и cost (веха 11.6, куча и кэши) --------------------------
#
#	python3 jobprobe.py heap > ref-heap.txt
#	python3 jobprobe.py cost > ref-cost.txt
#
# Режим heap - эталон роста кучи (PANFROST_BO_HEAP): panfrost не даёт ей
# памяти при создании и добавляет по 2 МиБ на каждый отказ трансляции
# внутри (panfrost_mmu_map_fault_addr()), не выключая адресного
# пространства.  Задания пишут в первый кусок, в тот же, в последний,
# одно задание - через 16 новых кусков, потом за конец кучи (отказ,
# которого кучей не решить) и цепочка после.  Строки без «#» -
# те же, что печатает pftest heap на MEECHO; с «#» - только здесь:
# регистры MMU, слота и окна во время первого роста (каждое изменение,
# время от SUBMIT), строки dmesg (с dynamic debug для
# panfrost_mmu_map_fault_addr: «mapped page fault @ AS0 ...»).
#
# Режим cost - то же, что pftest cost: цепочка с большим буфером в
# списке задания, не отображённым и отображённым.  Под Linux отображение
# буфера panfrost без WB_MMAP - write-combine, и чистить нечего; разница
# здесь - эталон «ничего», с которым сравнивается чистка службы.

HEAP_SIZE = 0x4000000           # 64 МиБ: 32 куска
PIECE = 0x200000
DDBG = "/sys/kernel/debug/dynamic_debug/control"


def chain_to(m, va, ats, tag):
    """Цепочка: задание i пишет tag<<24|i по адресу ats[i]."""
    n = len(ats)
    for i, at in enumerate(ats):
        nxt = va + JOB_SIZE * (i + 1) if i + 1 < n else 0
        write_value_job(m, JOB_SIZE * i, i + 1, nxt, at, (tag << 24) | i)


def headers_done(m, n):
    return sum(1 for i in range(n)
               if struct.unpack_from("<I", m, JOB_SIZE * i)[0] == 1)


def dyndbg(on):
    """dev_dbg() отказа кучи в dmesg; True, если удалось."""
    try:
        if not os.path.exists(DDBG):
            os.system("mount -t debugfs none /sys/kernel/debug "
                      "2>/dev/null")
        with open(DDBG, "w") as f:
            f.write("func panfrost_mmu_map_fault_addr %sp\n" %
                    ("+" if on else "-"))
        return True
    except OSError as e:
        print("# dynamic debug: %s" % e)
        return False


def show_timeline(tl, a):
    names = [nm for nm, _ in timeline_regs(a)]
    print("#   %9s  %s" % ("t, us", " ".join("%-10s" % nm for nm in names)))
    for e in tl:
        if e[1] is None:
            print("#   (%d polls in %.3f s)" % (e[2], e[0]))
            continue
        print("#   %9.0f  %s" % (e[0] * 1e6,
              " ".join("0x%08x" % v for v in e[1])))


def probe_heap():
    node = render_node()
    fd = os.open(node, os.O_RDWR)
    print("render node %s, mode heap" % node)
    g = gpu_regs()
    dmesg0 = os.popen("dmesg").read().splitlines()
    dbg = dyndbg(True)

    hb, vab = create(fd, 0x4000, 0)
    off, m = map_bo(fd, hb, 0x4000)
    hh, vah = create(fd, HEAP_SIZE, BO_NOEXEC | BO_HEAP)
    so = sync_create(fd)

    # Цепочка до кучи: проверка, и номер адресного пространства.
    chain_into(m, vab, 64, 0x5a)
    r, e = submit(fd, vab, [hb], out=so)
    sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
    print("chain before: submit %s, %d wrong word(s)" % (e or "ok",
          wrong(m, 64, 0x5a)))
    a = rd(g, 0x1818 + 0x80) & 0xf
    dump_mmu(g, "address space %d before the heap is touched" % a)

    tl = []
    for i, (at, what) in enumerate(((0x1000, ""),
                                    (0x1ff040, " (same piece)"),
                                    (HEAP_SIZE - 0x40, " (last piece)"))):
        chain_to(m, vab, [vah + at], 0x40 + i)
        r, e = submit(fd, vab, [hb, hh], out=so)
        if i == 0:
            timeline(g, a, 0.3, tl)
        sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
        print("heap write at +0x%x%s: submit %s, syncobj %s, header status "
              "0x%x" % (at, what, e or "ok", sw,
                        struct.unpack_from("<I", m, 0)[0]))

    ats = [vah + (i + 1) * PIECE + 0x40 * (i + 1) for i in range(16)]
    chain_to(m, vab, ats, 0x50)
    t = now_ns()
    r, e = submit(fd, vab, [hb, hh], out=so)
    sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
    dt = (now_ns() - t) / 1e6
    print("chain of 16 through pieces 1-16: submit %s, syncobj %s, %d of 16 "
          "header(s) done" % (e or "ok", sw, headers_done(m, 16)))
    print("(16 pieces in %.1f ms)" % dt)
    dump_mmu(g, "address space %d after 19 pieces" % a)

    # Заголовок - с нулевым словом состояния: с ненулевым ускоритель
    # отвергает дескриптор сам (0x58, DATA_INVALID_FAULT), до записи.
    chain_to(m, vab, [vah + HEAP_SIZE + 0x40], 0x60)
    r, e = submit(fd, vab, [hb, hh], out=so)
    wb = wait_bo(fd, hb, now_ns() + 2 * 10**9)
    sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
    print("write past the heap (+0x%x): submit %s, wait_bo %s, syncobj %s, "
          "header status 0x%x" % (HEAP_SIZE + 0x40, e or "ok", wb, sw,
                                  struct.unpack_from("<I", m, 0)[0]))

    time.sleep(1)
    chain_into(m, vab, 64, 0x6b)
    r, e = submit(fd, vab, [hb, hh], out=so)
    sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
    print("chain after: submit %s, %d wrong word(s)" % (e or "ok",
          wrong(m, 64, 0x6b)))

    print("\n# registers during the first heap write, every change "
          "(t from SUBMIT):")
    show_timeline(tl, a)
    m.close()
    os.close(fd)
    if dbg:
        dyndbg(False)
    dmesg1 = os.popen("dmesg").read().splitlines()
    new = dmesg1[len(dmesg0):] if dmesg1[:len(dmesg0)] == dmesg0 else \
        [l for l in dmesg1 if l not in set(dmesg0)]
    print("\n# dmesg, new lines:\n" + "\n".join("# " + l for l in new))
    return 0


def chains(fd, m, va, handles, so, n):
    """n цепочек из 64: (мс на цепочку, плохих)."""
    t0 = now_ns()
    bad = 0
    for k in range(n):
        chain_into(m, va, 64, 0x5a)
        r, e = submit(fd, va, handles, out=so)
        sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
        if e or sw != "ok" or wrong(m, 64, 0x5a):
            bad += 1
    return (now_ns() - t0) / 1e6 / n, bad


def probe_cost(n=200):
    fd = os.open(render_node(), os.O_RDWR)
    hb, vab = create(fd, 0x4000, 0)
    off, m = map_bo(fd, hb, 0x4000)
    so = sync_create(fd)
    plain, bad = chains(fd, m, vab, [hb], so, n)
    print("cost: %d chain(s) of 64, alone: %.3f ms each, %d bad" % (n, plain,
          bad))
    for size in (0x4000, 0x100000, 0x400000, 0x1000000, 0x4000000):
        k = n // 10 if size >= 0x1000000 and n >= 100 else n
        hg, vag = create(fd, size, BO_NOEXEC)
        cold, bad2 = chains(fd, m, vab, [hb, hg], so, k)
        offg, mg = map_bo(fd, hg, size)
        mg[0] = 1
        mapped, bad3 = chains(fd, m, vab, [hb, hg], so, k)
        print("cost: + %5d KiB buffer, %d chains: %.3f ms unmapped, %.3f ms "
              "mapped (+%.3f ms, %.2f ms per MiB), %d bad" % (size // 1024,
              k, cold, mapped, mapped - cold,
              (mapped - cold) * 1048576.0 / size, bad2 + bad3))
        mg.close()
        ioctl(fd, GEM_CLOSE, "<II", hg, 0)
    m.close()
    os.close(fd)
    return 0


# ---- режимы api и two (веха 11.6) -----------------------------------------

import ctypes
import errno as _errno


def err(e):
    return _errno.errorcode.get(e.errno, str(e.errno))


def try_ioctl(fd, req, fmt, *vals):
    """(значения, None) или (None, имя ошибки)."""
    try:
        return ioctl(fd, req, fmt, *vals), None
    except OSError as e:
        return None, err(e)


def now_ns():
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC)


def addr_of(buf):
    return ctypes.addressof(buf)


def u32_array(vals):
    a = (ctypes.c_uint32 * max(1, len(vals)))(*vals)
    return a, ctypes.addressof(a)


def create(fd, size, flags):
    v, e = try_ioctl(fd, CREATE_BO, "<IIIIQ", size, flags, 0, 0, 0)
    if e:
        print("create 0x%x flags %d: %s" % (size, flags, e))
        return None
    print("create 0x%x flags %d: handle %d va 0x%x" % (size, flags, v[2],
          v[4]))
    return v[2], v[4]


def map_bo(fd, handle, size):
    off = ioctl(fd, MMAP_BO, "<IIQ", handle, 0, 0)[2]
    return off, mmap.mmap(fd, size, mmap.MAP_SHARED,
                          mmap.PROT_READ | mmap.PROT_WRITE, offset=off)


def submit(fd, jc, handles, out=0, req=0, in_syncs=()):
    ha, hp = u32_array(handles)
    sa, sp = u32_array(list(in_syncs))
    return try_ioctl(fd, SUBMIT, "<QQIIQII", jc, sp if in_syncs else 0,
                     len(in_syncs), out, hp, len(handles), req)


def wait_bo(fd, handle, deadline):
    v, e = try_ioctl(fd, WAIT_BO, "<IIq", handle, 0, deadline)
    return e or "ok"


def sync_create(fd, flags=0):
    return ioctl(fd, SYNCOBJ_CREATE, "<II", 0, flags)[0]


def sync_wait(fd, handles, flags, deadline):
    ha, hp = u32_array(handles)
    v, e = try_ioctl(fd, SYNCOBJ_WAIT, "<QqIIIIQ", hp, deadline,
                     len(handles), flags, 0, 0, 0)
    return (e or "ok"), (v[4] if v else None)


def sync_array(fd, req, handles):
    ha, hp = u32_array(handles)
    v, e = try_ioctl(fd, req, "<QII", hp, len(handles), 0)
    return e or "ok"


def chain_into(m, va, n, tag, base=0x1000):
    for i in range(n):
        m[base + 4 * i:base + 4 * i + 4] = struct.pack("<I", 0)
    for i in range(n):
        nxt = va + JOB_SIZE * (i + 1) if i + 1 < n else 0
        write_value_job(m, JOB_SIZE * i, i + 1, nxt, va + base + 4 * i,
                        (tag << 24) | i)


def wrong(m, n, tag, base=0x1000):
    words = struct.unpack_from("<%dI" % n, m, base)
    return sum(1 for i in range(n) if words[i] != ((tag << 24) | i))


def probe_api():
    node = render_node()
    fd = os.open(node, os.O_RDWR)
    print("render node %s" % node)
    dmesg0 = os.popen("dmesg").read().splitlines()

    # Версия: строки по указателям, длины назад.
    name = ctypes.create_string_buffer(32)
    date = ctypes.create_string_buffer(32)
    desc = ctypes.create_string_buffer(32)
    v = ioctl(fd, VERSION, "<iii4xQQQQQQ", 0, 0, 0, 32, addr_of(name), 32,
              addr_of(date), 32, addr_of(desc))
    print("version: %d.%d.%d name '%s' (%d) date '%s' (%d) desc '%s' (%d)" %
          (v[0], v[1], v[2], name.value.decode(), v[3], date.value.decode(),
           v[5], desc.value.decode(), v[7]))
    name = ctypes.create_string_buffer(32)
    v = ioctl(fd, VERSION, "<iii4xQQQQQQ", 0, 0, 0, 4, addr_of(name), 0, 0,
              0, 0)
    print("version, name buffer of 4: '%s', name_len %d" % (
          name.value.decode(), v[3]))

    for cap in (0x13, 0x5, 0x6, 0x14, 0x7777):
        r, e = try_ioctl(fd, GET_CAP, "<QQ", cap, 0)
        print("cap 0x%x: %s" % (cap, e or r[1]))

    for p in range(45):
        r, e = try_ioctl(fd, GET_PARAM, "<IIQ", p, 0, 0)
        print("param %d: %s" % (p, e or "0x%x" % r[2]))
    r, e = try_ioctl(fd, GET_PARAM, "<IIQ", 0, 1, 0)
    print("param 0 with pad 1: %s" % (e or "0x%x" % r[2]))

    # Места буферов: та же последовательность, что у drmtest.c.
    b = {}
    b[0] = create(fd, 0x4000, 0)
    b[1] = create(fd, 0x1000, BO_NOEXEC)
    b[2] = create(fd, 0x300000, BO_NOEXEC)
    b[3] = create(fd, 0x19000, 0)
    b[4] = create(fd, 0x100000, BO_NOEXEC | BO_HEAP)
    create(fd, 0, 0)
    create(fd, 0x1000, BO_HEAP)
    create(fd, 0x1000, 8)
    b[5] = create(fd, 5000, BO_NOEXEC)
    for h in (b[3][0], 99):
        r, e = try_ioctl(fd, GET_BO_OFFSET, "<IIQ", h, 0, 0)
        print("get_bo_offset %d: %s" % (h, e or "0x%x" % r[2]))
    for h, f in ((b[0][0], 0), (b[4][0], 0), (b[0][0], 1)):
        r, e = try_ioctl(fd, MMAP_BO, "<IIQ", h, f, 0)
        print("mmap_bo %d flags %d: %s" % (h, f, e or "token 0x%x" % r[2]))
    for i in range(2):
        r, e = try_ioctl(fd, GEM_CLOSE, "<II", b[1][0], 0)
        print("gem_close %d%s: %s" % (b[1][0], " again" if i else "",
              e or "ok"))
    b[1] = create(fd, 0x1000, BO_NOEXEC)
    for h, madv in ((b[0][0], 1), (b[0][0], 7), (77, 0)):
        r, e = try_ioctl(fd, MADVISE, "<III", h, madv, 0)
        print("madvise %d %d: %s" % (h, madv, e or "retained %d" % r[2]))

    print("wait_bo %d idle, deadline 0: %s" % (b[0][0], wait_bo(fd, b[0][0],
          0)))
    print("wait_bo 99: %s" % wait_bo(fd, 99, 0))

    # Объекты синхронизации и цепочка с ними.
    so = sync_create(fd)
    print("syncobj create: handle %d" % so)
    print("syncobj wait %d (no fence): %s" % (so, sync_wait(fd, [so], 0,
          now_ns() + 10**9)[0]))
    print("syncobj wait %d for submit, 1 ms: %s" % (so, sync_wait(fd, [so],
          WAIT_FOR_SUBMIT, now_ns() + 10**6)[0]))
    h0, va0 = b[0]
    off, m = map_bo(fd, h0, 0x4000)
    chain_into(m, va0, 64, 0x5a)
    r, e = submit(fd, va0, [h0], out=so)
    wb = wait_bo(fd, h0, now_ns() + 2 * 10**9)
    sw, first = sync_wait(fd, [so], 0, now_ns() + 10**9)
    print("chain of 64 with out_sync %d: submit %s, wait_bo %s, syncobj "
          "wait %s first %s, %d wrong word(s)" % (so, e or "ok", wb, sw,
          first, wrong(m, 64, 0x5a)))
    print("syncobj reset %d: %s; wait: %s" % (so, sync_array(fd,
          SYNCOBJ_RESET, [so]), sync_wait(fd, [so], 0, now_ns())[0]))
    print("syncobj signal %d: %s; wait: %s" % (so, sync_array(fd,
          SYNCOBJ_SIGNAL, [so]), sync_wait(fd, [so], 0, now_ns())[0]))
    so2 = sync_create(fd, 1)
    print("syncobj create signaled: handle %d; wait all %d %d: %s" % (so2,
          so, so2, sync_wait(fd, [so, so2], WAIT_ALL, now_ns())[0]))
    r, e = try_ioctl(fd, SYNCOBJ_DESTROY, "<II", so, 0)
    print("syncobj destroy %d: %s; wait: %s" % (so, e or "ok", sync_wait(fd,
          [so], 0, now_ns())[0]))

    so = sync_create(fd)
    print("submit jc 0: %s" % (submit(fd, 0, [h0])[1] or "ok"))
    print("submit bo 99: %s" % (submit(fd, va0, [99])[1] or "ok"))
    r, e = try_ioctl(fd, SUBMIT, "<QQIIQII", va0, 0, 0, 0, 0x10, 1, 0)
    print("submit bad array: %s" % (e or "ok"))
    print("submit out_sync 77: %s" % (submit(fd, va0, [h0], out=77)[1] or
          "ok"))
    print("submit requirements 4: %s" % (submit(fd, va0, [h0], req=4)[1] or
          "ok"))
    wait_bo(fd, h0, now_ns() + 2 * 10**9)

    # Долгая цепочка: занятый буфер со сроком в прошлом и со сроком 1 мс.
    n = 32768
    hl, val = create(fd, 0x400000, 0)
    offl, ml = map_bo(fd, hl, 0x400000)
    for i in range(n):
        nxt = val + JOB_SIZE * (i + 1) if i + 1 < n else 0
        write_value_job(ml, JOB_SIZE * i, (i % 65535) + 1, nxt, val +
                        0x3ff000 + 4 * (i % 1024), i)
    submit(fd, val, [hl])
    a = wait_bo(fd, hl, 0)
    t = now_ns()
    c = wait_bo(fd, hl, t + 10**6)
    d = wait_bo(fd, hl, now_ns() + 5 * 10**9)
    print("long chain of %d: wait_bo deadline past: %s; deadline +1 ms: %s; "
          "then: %s (%.1f ms)" % (n, a, c, d, (now_ns() - t) / 1e6))

    # Отказ задания: что видят WAIT_BO и ожидание объекта.
    m[0x3000:0x3008] = struct.pack("<II", 0xdeadbeef, 0xdeadbeef)
    write_value_job(m, 0, 1, 0, va0 + API_FAULT_OFF, FAULT_VALUE)
    r, e = submit(fd, va0, [h0], out=so)
    wb = wait_bo(fd, h0, now_ns() + 2 * 10**9)
    sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
    print("fault at 0x%x with out_sync %d: submit %s; wait_bo %s; syncobj "
          "wait %s; header status 0x%x" % (va0 + API_FAULT_OFF, so,
          e or "ok", wb, sw, struct.unpack_from("<I", m, 0)[0]))
    chain_into(m, va0, 64, 0x6b)
    submit(fd, va0, [h0], out=so)
    wait_bo(fd, h0, now_ns() + 2 * 10**9)
    print("chain after the fault: %d wrong word(s)" % wrong(m, 64, 0x6b))

    # Куча: задание пишет в неё, первые 2 МиБ и следующие.  Через секунду:
    # panfrost платы через ~0.5 с после отказа ловит таймаут планировщика и
    # сбрасывает ускоритель, и задание, попавшее на сброс, теряется молча
    # (заголовок 0, ожидания - ok); куча проверяется не на этом.
    time.sleep(1)
    hh, vah = create(fd, 0x400000, BO_NOEXEC | BO_HEAP)
    for at in (0x1000, 0x300000):
        write_value_job(m, 0, 1, 0, vah + at, 0x12345678)
        r, e = submit(fd, va0, [h0, hh], out=so)
        wb = wait_bo(fd, h0, now_ns() + 2 * 10**9)
        sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
        print("heap write at +0x%x: submit %s, wait_bo %s, syncobj %s, "
              "header status 0x%x" % (at, e or "ok", wb, sw,
              struct.unpack_from("<I", m, 0)[0]))

    m.close()
    ml.close()
    os.close(fd)
    dmesg1 = os.popen("dmesg").read().splitlines()
    new = dmesg1[len(dmesg0):] if dmesg1[:len(dmesg0)] == dmesg0 else \
        [l for l in dmesg1 if l not in set(dmesg0)]
    print("\ndmesg, new lines:\n" + "\n".join(new))
    return 0


def client(tag, rounds, fault_at, out):
    """Один клиент: своё открытие узла, свой буфер, rounds цепочек."""
    fd = os.open(render_node(), os.O_RDWR)
    v = ioctl(fd, CREATE_BO, "<IIIIQ", 0x4000, 0, 0, 0, 0)
    h, va = v[2], v[4]
    off, m = map_bo(fd, h, 0x4000)
    so = sync_create(fd)
    bad = 0
    lines = ["client %s: va 0x%x" % (tag, va)]
    for k in range(rounds):
        if k == fault_at:
            write_value_job(m, 0, 1, 0, va + FAULT_OFF, FAULT_VALUE)
            submit(fd, va, [h], out=so)
            wb = wait_bo(fd, h, now_ns() + 2 * 10**9)
            sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
            lines.append("client %s: fault at round %d: wait_bo %s, syncobj "
                         "wait %s" % (tag, k, wb, sw))
            continue
        chain_into(m, va, 64, (tag == "A") and 0xa0 or 0xb0)
        submit(fd, va, [h], out=so)
        sw, first = sync_wait(fd, [so], 0, now_ns() + 2 * 10**9)
        if sw != "ok" or wrong(m, 64, (tag == "A") and 0xa0 or 0xb0):
            bad += 1
    lines.append("client %s: %d chains of 64, %d bad" % (tag, rounds -
                 (1 if fault_at is not None else 0), bad))
    m.close()
    os.close(fd)
    os.write(out, ("\n".join(lines) + "\n").encode())


def probe_two():
    g = gpu_regs()
    dmesg0 = os.popen("dmesg").read().splitlines()
    pa, pb = os.pipe(), os.pipe()
    kids = []
    for tag, fa, p in (("A", 50, pa), ("B", None, pb)):
        pid = os.fork()
        if pid == 0:
            os.close(p[0])
            client(tag, 200, fa, p[1])
            os._exit(0)
        os.close(p[1])
        kids.append((pid, p[0]))
    seen = {0: set(), 1: set()}
    t0 = time.monotonic()
    left = set(pid for pid, _ in kids)
    while left and time.monotonic() - t0 < 60:
        for js in (0, 1):
            cfg = rd(g, 0x1818 + 0x80 * js)
            if cfg:
                seen[js].add(cfg & 0xf)
        for pid in list(left):
            if os.waitpid(pid, os.WNOHANG)[0] == pid:
                left.discard(pid)
    out = []
    for pid, fdr in kids:
        if pid in left:
            os.waitpid(pid, 0)
        out.append(os.read(fdr, 65536).decode())
    print("".join(out).rstrip())
    print("address spaces seen in JS1 CONFIG: %s; in JS0: %s" % (
          sorted(seen[1]), sorted(seen[0])))
    dmesg1 = os.popen("dmesg").read().splitlines()
    new = dmesg1[len(dmesg0):] if dmesg1[:len(dmesg0)] == dmesg0 else \
        [l for l in dmesg1 if l not in set(dmesg0)]
    print("\ndmesg, new lines:\n" + "\n".join(new))
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["api"]:
        sys.exit(probe_api())
    if sys.argv[1:] == ["two"]:
        pc = DEV + "/power/control"
        orig = open(pc).read().strip()
        open(pc, "w").write("on")
        try:
            sys.exit(probe_two())
        finally:
            open(pc, "w").write(orig)
    if sys.argv[1:] in (["heap"], ["cost"]):
        pc = DEV + "/power/control"
        orig = open(pc).read().strip()
        open(pc, "w").write("on")
        time.sleep(0.2)
        try:
            sys.exit(probe_heap() if sys.argv[1] == "heap" else probe_cost())
        finally:
            open(pc, "w").write(orig)
    sys.exit(main())
