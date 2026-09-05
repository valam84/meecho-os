#!/usr/bin/env python3
# Regenerate the padding[] arrays of the message payload variants in
# <minix/ipc.h>.
#
# Every variant is filled out to the payload size with an explicit padding
# array, so that the space left in a message is visible in the header and
# taking some of it is a deliberate act.  Widening the payload therefore means
# recomputing every one of those arrays, which is what this script does.  The edit is
# mechanical and must stay reproducible: the header is the generator's output,
# not something to correct by hand.  Run it again after any change to a
# variant's fields, or to the payload size.
#
# The payload size is read from <minix/ipcconst.h> so that it is stated once,
# in the header the rest of the system already includes.
#
# Padding is computed for LP64, the data model this fork targets.  A 32-bit
# build lays the same fields out smaller and its variants come out short of
# the payload, which _ASSERT_MSG_SIZE allows and the union's own size[] array
# covers: sizeof(message) is the same on both.  Making a variant exact under
# both data models at once would take a padding value per model, i.e. an ABI
# conditional in every variant, to no benefit -- 32-bit is a reference build.
#
# Usage: python3 gen-msgpadding.py [--check] [path/to/ipc.h]
#        --check verifies that the header already matches; it writes nothing
#        and exits nonzero if it does not.

import re
import sys
from collections import Counter

import msgpayload as mp

# The four raw views of the payload.  They have no padding: they *are* the
# padding, one element type each, and must span the payload exactly.
RAW = {"mess_u8": ("uint8_t", 1), "mess_u16": ("uint16_t", 2),
       "mess_u32": ("uint32_t", 4), "mess_u64": ("uint64_t", 8)}

PAD_LINE_RE = re.compile(r"^[ \t]*[A-Za-z_][A-Za-z0-9_]*[ \t]+%s\[[0-9]+\];[ \t]*$"
                         % mp.PAD)
DATA_LINE_RE = re.compile(r"^([ \t]*)([A-Za-z_][A-Za-z0-9_]*)([ \t]+)data\[[0-9]+\];[ \t]*$")


def regenerate(fieldtext, name, size):
    """Return the variant's body with its padding refilled to `size` bytes."""
    lines = fieldtext.split("\n")

    if name in RAW:
        elem, width = RAW[name]
        if size % width:
            sys.exit("%s: payload of %d bytes is not a whole number of %s"
                     % (name, size, elem))
        out = []
        for ln in lines:
            m = DATA_LINE_RE.match(ln)
            out.append("%s%s%sdata[%d];" % (m.group(1), m.group(2), m.group(3),
                                            size // width) if m else ln)
        return out == lines, "\n".join(out)

    kept = [ln for ln in lines if not PAD_LINE_RE.match(ln)]
    fields = mp.unpadded(mp.parse("\n".join(kept)))
    room = size - mp.tail(fields, "lp64")
    if room < 0:
        sys.exit("%s: content is %d bytes, payload is %d -- this variant needs "
                 "a re-cut, not padding" % (name, size - room, size))
    if room:
        kept.append("\tuint8_t %s[%d];" % (mp.PAD, room))
    return kept == lines, "\n".join(kept)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    check = "--check" in sys.argv[1:]
    src = args[0] if args else mp.DEFAULT_SRC

    text = mp.read_source(src)
    size = mp.payload_size(src)
    start, end, body, blocks, names = mp.extract(text)
    before = {n: mp.unpadded(mp.parse(f)) for f, n in blocks}

    # ------------------------------------------------------------- rewrite
    out, pos, changed, room = [], 0, [], {}
    for m in mp.BLOCK_RE.finditer(body):
        same, newtext = regenerate(m.group(1), m.group(2), size)
        if not same:
            changed.append(m.group(2))
        out.append(body[pos:m.start(1)])
        out.append(newtext)
        pos = m.end(1)
    out.append(body[pos:])
    newbody = "".join(out)

    if len(blocks) != len(names):
        sys.exit("extraction disagrees with itself: %d blocks, %d names"
                 % (len(blocks), len(names)))

    # ------------------------------------------------------------- verify
    # Nothing but padding may have moved, and the compiler -- not the layout
    # engine in msgpayload.py -- has the final word on the sizes.
    _, _, _, newblocks, newnames = mp.extract(text[:start] + newbody + text[end:])
    if newnames != names:
        sys.exit("regeneration lost or renamed a variant")
    lost = []
    for f, n in newblocks:
        a, b = before[n], mp.unpadded(mp.parse(f))
        if n in RAW:
            a = [(t, nm, 0, p) for t, nm, _, p in a]
            b = [(t, nm, 0, p) for t, nm, _, p in b]
        if a != b:
            lost.append(n)
    if lost:
        sys.exit("regeneration changed the fields of: %s" % ", ".join(lost))

    sizes = mp.compile_sizes(newbody, newnames)
    wrong64 = [(n, s) for n, s in zip(newnames, sizes["lp64"]) if s != size]
    wrong32 = [(n, s) for n, s in zip(newnames, sizes["ilp32"]) if s > size]
    for label, wrong in (("LP64 not exactly %d" % size, wrong64),
                         ("ILP32 over %d" % size, wrong32)):
        if wrong:
            print("%s: %d variants" % (label, len(wrong)))
            for n, s in wrong[:10]:
                print("   %-44s %d" % (n, s))
    if wrong64 or wrong32:
        sys.exit(1)

    for f, n in newblocks:
        p = [x for x in mp.parse(f) if x[1] == mp.PAD]
        room[n] = p[0][2] if p else 0

    # ------------------------------------------------------------- report
    print("payload %d bytes, %d variants, %d rewritten"
          % (size, len(names), len(changed)))
    print("compiler: LP64 all %d, ILP32 max %d, min %d"
          % (size, max(sizes["ilp32"]), min(sizes["ilp32"])))
    print("fields: unchanged in all %d variants" % len(names))
    full = [n for n in names if room[n] == 0]
    print("free bytes left, LP64: min %d (%d variants full), max %d, median %d"
          % (min(room.values()), len(full), max(room.values()),
             sorted(room.values())[len(room) // 2]))
    hist = Counter(room.values())
    print("   " + "  ".join("%d:%d" % (k, hist[k])
                            for k in sorted(hist, reverse=True)[:12]))
    if full:
        print("   full: %s" % ", ".join(sorted(full)))

    if newbody == body:
        print("%s is already up to date" % src)
        return 0
    if check:
        print("%s needs regeneration (%d variants)" % (src, len(changed)))
        return 1
    open(src, "w").write(text[:start] + newbody + text[end:])
    print("wrote %s" % src)
    return 0


if __name__ == "__main__":
    sys.exit(main())
