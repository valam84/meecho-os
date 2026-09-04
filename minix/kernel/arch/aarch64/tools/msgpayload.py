#!/usr/bin/env python3
# Shared reader for the message payload variants of <minix/ipc.h>.
#
# Both stage 3 tools stand on this module: msgsize-survey.py, which measures
# what the variants cost under each data model, and gen-msgpadding.py, which
# rewrites their padding[] arrays.  They must agree on what a variant is and
# how it is laid out, so the extraction, the type table and the layout rules
# live here once.
#
# The MINIX types the payloads use are declared here rather than pulled from
# the tree: <minix/ipc.h> cannot be preprocessed for aarch64 without a full
# set of machine headers, and the point of these tools is to answer questions
# about the header itself, not about one build of it.  The widths follow
# sys/sys/types.h, sys/sys/ansi.h and minix/include/minix/type.h.  What
# validates them is the ILP32 column: with a 32-bit long the same declarations
# must reproduce the tree's own historic assertion, 56 bytes for all 256.
#
# Sizes are not printed by a running program: there is no aarch64 libc here
# and no user-mode emulator.  They are compiled into a .probe section and read
# back with objcopy.  A hand-written layout engine runs beside the compiler so
# the two can be checked against each other.

import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SRC = os.path.normpath(
    os.path.join(HERE, "../../../../include/minix/ipc.h"))
DEFAULT_OUT = "/tmp/msgprobe"

CC = os.environ.get("MSGPROBE_CC", "aarch64-linux-gnu-gcc")
OBJCOPY = os.environ.get("MSGPROBE_OBJCOPY", "aarch64-linux-gnu-objcopy")

# ---------------------------------------------------------------- extraction
#
# The variants are one run of typedefs in the middle of the header, from the
# first one to the last one before the message union itself.  They are found
# by their names rather than by any one field, so that a tool may rewrite the
# fields and still find them again afterwards.

FIRST_VARIANT = "} mess_u8;"
AFTER_LAST = "typedef struct noxfer_message"
BLOCK_RE = re.compile(r"typedef struct \{\n(.*?)\n\} (mess_[A-Za-z0-9_]+);",
                      re.S)


def read_source(path=None):
    return open(path or DEFAULT_SRC).read()


def payload_size(ipc_h=None):
    """Read M_PAYLOAD_SIZE out of <minix/ipcconst.h> beside <minix/ipc.h>.

    The payload size is stated once, in the header the system already
    includes; the tools take it from there rather than each carrying a copy.
    """
    const_h = os.path.join(os.path.dirname(ipc_h or DEFAULT_SRC),
                           "ipcconst.h")
    m = re.search(r"^#define\s+M_PAYLOAD_SIZE\s+(\d+)", open(const_h).read(),
                  re.M)
    if not m:
        sys.exit("no M_PAYLOAD_SIZE in %s" % const_h)
    return int(m.group(1))


def extract(text):
    """Locate the run of payload typedefs.

    Returns (start, end, body, blocks, names): the span of the run in the
    file, its text, the [(fieldtext, name)] pairs in it and the names alone.
    """
    anchor = text.index(FIRST_VARIANT)
    start = text.rindex("typedef struct {", 0, anchor)
    end = text.index(AFTER_LAST, anchor)
    body = text[start:end]
    blocks = BLOCK_RE.findall(body)
    return start, end, body, blocks, [n for _, n in blocks]


# ------------------------------------------------------------- field parsing
# (size, alignment); "L" means "one machine long", i.e. it follows the ABI.
T = {
    "char": (1, 1), "signed char": (1, 1), "unsigned char": (1, 1),
    "short": (2, 2), "unsigned short": (2, 2),
    "int": (4, 4), "unsigned int": (4, 4), "unsigned": (4, 4),
    "long": ("L", "L"), "unsigned long": ("L", "L"),
    "long int": ("L", "L"), "unsigned long int": ("L", "L"),
    "long unsigned int": ("L", "L"),
    "int8_t": (1, 1), "uint8_t": (1, 1), "u8_t": (1, 1),
    "int16_t": (2, 2), "uint16_t": (2, 2), "u16_t": (2, 2),
    "int32_t": (4, 4), "uint32_t": (4, 4), "u32_t": (4, 4),
    "int64_t": (8, 8), "uint64_t": (8, 8), "u64_t": (8, 8),
    "endpoint_t": (4, 4), "cp_grant_id_t": (4, 4),
    "devmajor_t": (4, 4), "devminor_t": (4, 4),
    "clockid_t": (4, 4), "clock_t": (4, 4),
    "pid_t": (4, 4), "uid_t": (4, 4), "gid_t": (4, 4), "mode_t": (4, 4),
    "dev_t": (8, 8), "ino_t": (8, 8), "off_t": (8, 8), "time_t": (8, 8),
    "size_t": ("L", "L"), "ssize_t": ("L", "L"), "key_t": ("L", "L"),
    "vir_bytes": ("L", "L"), "phys_bytes": ("L", "L"),
    "sigset_t": (16, 4), "fd_set": (32, 4),
    "union ds_val": (4, 4),
}
LONGW = {"ilp32": 4, "lp64": 8}
GROWS = {"long", "unsigned long", "long int", "unsigned long int",
         "long unsigned int", "size_t", "ssize_t", "key_t",
         "vir_bytes", "phys_bytes"}
CONST = {"M_PATH_STRING_MAX": 40, "CTL_SHORTNAME": 8, "NR_DOMAIN": 8,
         "NDEV_NAME_MAX": 16, "NDEV_HWADDR_MAX": 6, "NDEV_IOV_MAX": 8}

PAD = "padding"


def parse(fieldtext):
    """Return [(type, name, nelem, is_pointer)] for one struct body."""
    s = re.sub(r"/\*.*?\*/", " ", fieldtext, flags=re.S)
    out = []
    for decl in s.split(";"):
        decl = " ".join(decl.split())
        if not decl:
            continue
        m = re.match(r"^((?:const\s+)?(?:union\s+|struct\s+)?"
                     r"[A-Za-z_][A-Za-z0-9_]*"
                     r"(?:\s+(?:int|long|char|short))*)\s+(.*)$", decl)
        if not m:
            sys.exit("cannot parse declaration: %r" % decl)
        tname, rest = m.group(1), m.group(2)
        tname = re.sub(r"^const\s+", "", tname).strip()
        if tname == "void":
            tname = "char"		# only ever appears as void *
        for d in rest.split(","):
            d = d.strip()
            ptr = d.startswith("*")
            d = d.lstrip("* ")
            am = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\[([A-Za-z0-9_]+)\]$", d)
            if am:
                n = am.group(2)
                out.append((tname, am.group(1),
                            int(n) if n.isdigit() else CONST[n], ptr))
            else:
                out.append((tname, d, 1, ptr))
    return out


def measure(tname, ptr, abi):
    """(size, alignment) of one field under one ABI."""
    if ptr:
        return LONGW[abi], LONGW[abi]
    sz, al = T[tname]
    if sz == "L":
        sz = al = LONGW[abi]
    return sz, al


def tail(fields, abi):
    """Offset just past the last field, before the struct's own rounding.

    This is where a uint8_t padding[] would start, so it is what the padding
    generator subtracts from the payload size.
    """
    off = 0
    for tname, _, n, ptr in fields:
        sz, al = measure(tname, ptr, abi)
        off = (off + al - 1) // al * al
        off += sz * n
    return off


def alignment(fields, abi):
    return max([1] + [measure(t, p, abi)[1] for t, _, _, p in fields])


def layout(fields, abi):
    """Lay a parsed struct out by the C rules; return (size, [(name, off, sz)])."""
    off, maxa, detail = 0, 1, []
    for tname, name, n, ptr in fields:
        sz, al = measure(tname, ptr, abi)
        off = (off + al - 1) // al * al
        detail.append((name, off, sz * n))
        off += sz * n
        maxa = max(maxa, al)
    return (off + maxa - 1) // maxa * maxa, detail


def unpadded(fields):
    """The fields that carry meaning: everything but the padding array."""
    return [f for f in fields if f[1] != PAD]


def content(fields, abi):
    """Smallest the variant can be without touching what it says."""
    return layout(unpadded(fields), abi)[0]


# --------------------------------------------------------------- the C probe
PRELUDE = r"""
/* generated by msgpayload.py -- do not edit */
#include <stdint.h>
#define _ASSERT_MSG_SIZE(t) extern int _msgsz_unused_##t
#define M_PATH_STRING_MAX 40
#define CTL_SHORTNAME 8
#define NR_DOMAIN 8
#define NDEV_NAME_MAX 16
#define NDEV_HWADDR_MAX 6
#define NDEV_IOV_MAX 8
typedef int endpoint_t;
typedef int32_t cp_grant_id_t;
typedef unsigned long vir_bytes;
typedef unsigned long phys_bytes;
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;
typedef unsigned long size_t;
typedef long ssize_t;
typedef int64_t off_t;
typedef int64_t time_t;
typedef unsigned int clock_t;
typedef int clockid_t;
typedef uint64_t dev_t;
typedef uint64_t ino_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int32_t pid_t;
typedef long key_t;
typedef int32_t devmajor_t;
typedef int32_t devminor_t;
typedef struct { uint32_t __bits[4]; } sigset_t;
typedef struct { uint32_t fds_bits[8]; } fd_set;
"""


def write_probe(body, names, out=DEFAULT_OUT):
    """Write the C file that carries every variant's size in a section."""
    os.makedirs(out, exist_ok=True)
    probe = PRELUDE + body
    probe += ("\nconst uint32_t probe[] "
              "__attribute__((section(\".probe\"), used)) = {\n")
    probe += ",\n".join("\tsizeof(%s)" % n for n in names)
    probe += "\n};\n"
    path = out + "/probe.c"
    open(path, "w").write(probe)
    return path


def probe_sizes(abi, out=DEFAULT_OUT):
    """Compile the probe for one ABI and read the sizes back out of .probe."""
    flags = ["-mabi=ilp32"] if abi == "ilp32" else []
    obj = "%s/probe-%s.o" % (out, abi)
    raw = "%s/probe-%s.bin" % (out, abi)
    subprocess.run([CC, "-c", "-O2", "-ffreestanding", "-Wall"] + flags +
                   [out + "/probe.c", "-o", obj], check=True)
    subprocess.run([OBJCOPY, "-O", "binary", "-j", ".probe", obj, raw],
                   check=True)
    d = open(raw, "rb").read()
    return [int.from_bytes(d[i:i + 4], "little") for i in range(0, len(d), 4)]


def compile_sizes(body, names, out=DEFAULT_OUT):
    """{abi: [size per variant]} straight from the compiler, both ABIs."""
    write_probe(body, names, out)
    return {abi: probe_sizes(abi, out) for abi in ("ilp32", "lp64")}
