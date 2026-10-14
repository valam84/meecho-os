#!/usr/bin/env python3
"""Mesa's reachover, made from meson's own build graph.

    mesa-reachover.py BUILD DIST OUT

BUILD   a meson build directory configured for MEECHO and built as far as
        meson gets (port/gfx/mesa-import.sh setup, build): every object
        compiled, so that ninja holds what each one read
DIST    the unpacked Mesa release it was configured from
OUT     external/mit/mesa in the tree

Mesa's build is some 1150 compiles in 90 meson targets, with 130 generated
files made by Python and mako.  A hand-written list of that would be out of
date at the first import, so the lists are made from the build itself; the
decisions - flags MEECHO needs, what is left out - stay in the hand-written
Makefiles beside them.

What it writes:
  OUT/dist/...      the files of DIST the build reads, unchanged (sources,
                    headers, licences), and the public EGL/GLES/KHR headers
  OUT/gen/...       the generated files the build reads, at their paths in
                    BUILD; nir_opt_algebraic.c (52 MB of automaton tables,
                    300 KB compressed) as .gz, unpacked by the build with
                    TOOL_GZIP
  OUT/lib/<lib>/srcs.mk
                    for each library made: its sources, each built through a
                    symlink named by its path (BUILDSYMLINKS), and the flags
                    meson gave each of its targets
Products, as static libraries (the userland is static): libgallium (the
driver: Mesa, GLSL, NIR, Gallium, Panfrost), libEGL, libGLESv2, libgbm
(with its DRI backend: one library may be made of several meson targets).
"""

import collections
import gzip
import json
import os
import re
import shlex
import shutil
import subprocess
import sys

BIG = "src/compiler/nir/nir_opt_algebraic.c"	# committed gzipped

PRODUCTS = [
    # library, meson's link target, take its own objects, take its archives
    ("libgallium", "src/gallium/targets/dri/libgallium-26.2.4.so", True,
     True),
    ("libEGL", "src/egl/libEGL.so.1.0.0", True, False),
    ("libGLESv2", "src/mesa/glapi/es2api/libGLESv2.so.2.0.0", True, False),
    ("libgbm", "src/gbm/libgbm.so.1.0.0", True, False),
    # GBM's backend, which libgbm would dlopen() from /usr/lib/gbm: in a
    # static program it is linked into libgbm instead (lib/libgbm/Makefile)
    ("libgbm", "src/gbm/backends/dri/dri_gbm.so", True, False),
]

PUBLIC_HEADERS = ["include/EGL", "include/GLES2", "include/GLES3",
                  "include/KHR"]
LICENCES = ["docs/license.rst", "licenses"]

DROP = {"-pipe", "-pthread", "-fPIC", "-fpic", "-Winvalid-pch", "-MD",
        "-MMD", "-g", "-c"}
DROP_WITH_ARG = {"-o", "-MQ", "-MF", "-MT"}


def die(msg):
    sys.exit("mesa-reachover: " + msg)


class Build(object):
    def __init__(self, build, dist):
        self.b = os.path.abspath(build)
        self.dist = os.path.abspath(dist)
        self.cc = {c["output"]: c for c in json.load(
            open(os.path.join(self.b, "compile_commands.json")))}
        self.destdir = None

    def ninja(self, *args):
        return subprocess.check_output(("ninja",) + args, cwd=self.b,
                                       text=True)

    def link_inputs(self, target):
        cmd = self.ninja("-t", "commands", target).strip()
        words = cmd.splitlines()[-1].split()
        objs = [w for w in words if w.endswith(".o")]
        arcs = [w for w in words if w.endswith(".a") and
                not w.startswith("/")]
        return objs, arcs

    def where(self, path):
        """('dist', rel) or ('gen', rel) or ('sys', abs) for a path as
        meson wrote it (relative to BUILD)."""
        p = os.path.normpath(os.path.join(self.b, path))
        if p.startswith(self.dist + "/"):
            return "dist", p[len(self.dist) + 1:]
        if p.startswith(self.b + "/") or p == self.b:
            return "gen", os.path.relpath(p, self.b)
        return "sys", p


def target_of(output):
    """'src/util/libmesa_util.a.p/u_debug.c.o' -> 'src/util/libmesa_util.a'"""
    return output.split(".p/")[0]


def short(target):
    t = os.path.basename(target)
    for pre in ("lib",):
        if t.startswith(pre):
            t = t[len(pre):]
    return t.split(".")[0]


def flat(rel):
    rel = rel[4:] if rel.startswith("src/") else rel
    return rel.replace("/", "-")


def lang(path):
    return "cxx" if path.endswith((".cpp", ".cc", ".cxx")) else "c"


def mk_quote(arg):
    """An argument as make passes it on to sh: ${MESADIST} and the like are
    make's to expand, anything else sh might read is quoted."""
    if re.fullmatch(r"[\w@%+=:,./{}$-]+", arg):
        return arg
    if "$" in arg:
        die("cannot quote %r" % arg)
    return shlex.quote(arg)


def flags_of(bld, entry):
    """meson's command for one object -> (cppflags, copts), MEECHO paths."""
    words = shlex.split(entry["command"])[1:]
    src = entry["file"]
    cpp, opts = [], []
    skip = False
    for w in words:
        if skip:
            skip = False
            continue
        if w in DROP_WITH_ARG:
            skip = True
            continue
        if w in DROP or w == src or w.startswith(("-O", "-fdiagnostics-")):
            continue
        if w.startswith("-I"):
            kind, rel = bld.where(w[2:])
            if kind == "dist":
                cpp.append("-I${MESADIST}/" + rel)
            elif kind == "gen":
                cpp.append("-I${MESAGEN}/" + rel if rel != "." else
                           "-I${MESAGEN}")
            elif rel.startswith(bld.destdir + "/"):
                cpp.append("-I${DESTDIR}" + rel[len(bld.destdir):])
            else:
                die("include outside the build: " + w)
        elif w.startswith(("-D", "-U")):
            cpp.append(w)
        elif w.startswith(("-std=", "-f", "-W", "-m")):
            opts.append(w)
        else:
            die("unexpected flag %r in %s" % (w, entry["output"]))
    return cpp, opts


def copy(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def main():
    if len(sys.argv) != 4:
        die("usage: mesa-reachover.py BUILD DIST OUT")
    bld = Build(sys.argv[1], sys.argv[2])
    out = os.path.abspath(sys.argv[3])
    bld.destdir = os.environ.get("DESTDIR",
                                 os.path.expanduser("~/dest-evbarm64"))

    # Which objects each product is made of.
    libs = collections.OrderedDict()
    for lib, target, own, archives in PRODUCTS:
        objs, arcs = bld.link_inputs(target)
        chosen = list(objs) if own else []
        if archives:
            for a in arcs:
                chosen += sorted(o for o in bld.cc if o.startswith(a + ".p/"))
        for o in chosen:
            if o not in bld.cc:
                die("no compile command for " + o)
        have = libs.setdefault(lib, [])
        have += [o for o in chosen if o not in have]

    # What they read: ninja's dependency records, and the sources.
    every = sorted({o for objs in libs.values() for o in objs})
    deps = bld.ninja("-t", "deps", *every)
    dist, gen = set(), set()
    for line in deps.splitlines():
        if line.startswith("    "):
            kind, rel = bld.where(line.strip())
            if kind == "dist":
                dist.add(rel)
            elif kind == "gen":
                gen.add(rel)
    for o in every:
        kind, rel = bld.where(bld.cc[o]["file"])
        (dist if kind == "dist" else gen).add(rel)

    # Copy them.
    for d in ("dist", "gen"):
        shutil.rmtree(os.path.join(out, d), ignore_errors=True)
    for rel in sorted(dist):
        copy(os.path.join(bld.dist, rel), os.path.join(out, "dist", rel))
    for top in PUBLIC_HEADERS + LICENCES:
        src = os.path.join(bld.dist, top)
        if os.path.isdir(src):
            for root, _, files in os.walk(src):
                for f in files:
                    rel = os.path.relpath(os.path.join(root, f), bld.dist)
                    copy(os.path.join(bld.dist, rel),
                         os.path.join(out, "dist", rel))
        elif os.path.exists(src):
            copy(src, os.path.join(out, "dist", top))
    for rel in sorted(gen):
        src = os.path.join(bld.b, rel)
        if rel == BIG:
            dst = os.path.join(out, "gen", rel + ".gz")
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(src, "rb") as i, gzip.GzipFile(dst, "wb", 9,
                                                     mtime=0) as o:
                shutil.copyfileobj(i, o)
        else:
            copy(src, os.path.join(out, "gen", rel))

    # Names: a source's path, flattened; a target's name in front where one
    # source is built for several targets (Panfrost's per-architecture
    # libraries: the same file, PAN_ARCH 4 to 14).
    uses = collections.Counter()
    for lib, objs in libs.items():
        for o in objs:
            uses[bld.where(bld.cc[o]["file"])[1]] += 1

    for lib, objs in libs.items():
        groups = collections.OrderedDict()	# (target, lang) -> [(name, kind, rel, cpp, opts)]
        for o in objs:
            e = bld.cc[o]
            kind, rel = bld.where(e["file"])
            tgt = target_of(o)
            name = flat(rel)
            if uses[rel] > 1:
                name = short(tgt) + "--" + name
            cpp, opts = flags_of(bld, e)
            groups.setdefault((tgt, lang(rel)), []).append(
                (name, kind, rel, cpp, opts))

        # Flags every source of the library has, then each group's own.
        allf = [tuple(x[3] + x[4]) for g in groups.values() for x in g]
        common = set(allf[0])
        for f in allf[1:]:
            common &= set(f)
        names = set()
        lines = []
        w = lines.append
        w("# Generated by port/gfx/mesa-reachover.py from meson's build graph")
        w("# for this configuration: do not edit, run it again (port/gfx/README).")
        w("# %d sources in %d targets." % (len(objs), len(groups)))
        w("")
        first = allf[0]
        cpp_common = [f for f in first if f in common and
                      f.startswith(("-I", "-D", "-U"))]
        w("MESA_CPPFLAGS= \\")
        w("\t" + " \\\n\t".join(mk_quote(f) for f in cpp_common))
        w("")
        for (tgt, lg), items in groups.items():
            key = "%s.%s" % (short(tgt), lg)
            cpp0, opts0 = items[0][3], items[0][4]
            same = all(x[3] == cpp0 and x[4] == opts0 for x in items)
            w("# meson: %s (%s), %d source(s)" % (tgt, lg, len(items)))
            w("MESA_SRCS.%s= \\" % key)
            w("\t" + " \\\n\t".join(x[0] for x in items))
            for name, kind, rel, cpp, opts in items:
                if name in names:
                    die("two sources named " + name)
                names.add(name)
                # #include "x.h" looks beside the file as the compiler names
                # it - the symlink in the object directory - so the real
                # directory is named for it (and the source's own, for a
                # generated file: meson searches both).
                d = os.path.dirname(rel)
                quote = ["-iquote ${MESADIST}/" + d]
                if kind == "gen":
                    quote.insert(0, "-iquote ${MESAGEN}/" + d)
                w("CPPFLAGS.%s+=\t%s" % (name, " ".join(quote)))
                if kind == "gen" and rel == BIG:
                    continue	# unpacked by a rule in the Makefile
                base = "${MESADIST}" if kind == "dist" else "${MESAGEN}"
                w("BUILDSYMLINKS+=\t%s/%s %s" % (base, rel, name))
            own_cpp = [f for f in cpp0 if f not in cpp_common]
            w("MESA_CPPFLAGS.%s= \\" % key)
            w("\t" + " \\\n\t".join(mk_quote(f) for f in own_cpp) if own_cpp
              else "\t")
            w("MESA_COPTS.%s= \\" % key)
            w("\t" + " \\\n\t".join(mk_quote(f) for f in opts0) if opts0
              else "\t")
            w(".for _f in ${MESA_SRCS.%s}" % key)
            w("SRCS+=\t\t${_f}")
            w("CPPFLAGS.${_f}+=\t${MESA_CPPFLAGS} ${MESA_CPPFLAGS.%s}" % key)
            w("COPTS.${_f}+=\t${MESA_COPTS.%s}" % key)
            w(".endfor")
            if not same:
                for name, kind, rel, cpp, opts in items:
                    if (cpp, opts) == (cpp0, opts0):
                        continue
                    w("# %s differs from its target" % name)
                    w("CPPFLAGS.%s= ${MESA_CPPFLAGS} %s" % (
                        name, " ".join(mk_quote(f) for f in cpp
                                       if f not in cpp_common)))
                    w("COPTS.%s= %s" % (name, " ".join(mk_quote(f)
                                                       for f in opts)))
            w("")
        path = os.path.join(out, "lib", lib, "srcs.mk")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "w").write("\n".join(lines))
        print("%-12s %4d sources, %2d groups -> %s" % (
            lib, len(objs), len(groups), os.path.relpath(path, out)))
    print("dist: %d files, gen: %d files" % (len(dist), len(gen)))


if __name__ == "__main__":
    main()
