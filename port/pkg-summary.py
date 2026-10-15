#!/usr/bin/env python3
"""Сделать pkg_summary(5) репозитория из его пакетов.

    pkg-summary.py каталог-репозитория

Пишет рядом с пакетами pkg_summary.gz и pkg_summary.bz2 - то, что читает
pkgin (`pkgin update`). На NetBSD сводку делает `pkg_info -X *.tgz`, но
pkg_info собран для машины, а пакеты делаются на хосте (тот же довод, что
у mkpkg.sh), и формат прост: блок строк ПОЛЕ=значение на пакет,
пустая строка между блоками, многострочные поля повторяют имя поля в
каждой строке.

Поля - те, что pkg_info -X берёт из метаданных пакета (+CONTENTS,
+COMMENT, +DESC, +BUILD_INFO), плюс FILE_NAME и FILE_SIZE из самого
архива. pkgin проверяет MACHINE_ARCH и зависимости (DEPENDS из @pkgdep),
остальное показывает. SIZE_PKG - сумма размеров файлов, как +SIZE_PKG у
pkg_create.
"""
import bz2
import gzip
import os
import sys
import tarfile
import time

META = ("+CONTENTS", "+COMMENT", "+DESC", "+BUILD_INFO")


def entry(path):
    meta, size_pkg = {}, 0
    with tarfile.open(path, "r:*") as tf:
        for m in tf:
            if m.name in META:
                meta[m.name] = tf.extractfile(m).read().decode("utf-8", "replace")
            elif m.isfile():
                size_pkg += m.size
    if "+CONTENTS" not in meta:
        raise SystemExit(f"{path}: нет +CONTENTS - не пакет pkgsrc")

    name, deps, conflicts = None, [], []
    for line in meta["+CONTENTS"].splitlines():
        if line.startswith("@name "):
            name = line[6:].strip()
        elif line.startswith("@pkgdep "):
            deps.append(line[8:].strip())
        elif line.startswith("@pkgcfl "):
            conflicts.append(line[8:].strip())
    if not name:
        raise SystemExit(f"{path}: в +CONTENTS нет @name")

    info = {}
    for line in meta.get("+BUILD_INFO", "").splitlines():
        k, sep, v = line.partition("=")
        if sep:
            info[k.strip()] = v.strip()

    st = os.stat(path)
    out = [f"PKGNAME={name}"]
    out += [f"DEPENDS={d}" for d in deps]
    out += [f"CONFLICTS={c}" for c in conflicts]
    out.append("COMMENT=" + meta.get("+COMMENT", "").strip())
    out.append(f"SIZE_PKG={size_pkg}")
    out.append("BUILD_DATE=" + time.strftime("%Y-%m-%d %H:%M:%S +0000",
                                             time.gmtime(st.st_mtime)))
    pkgpath = info.get("PKGPATH", "")
    if pkgpath:
        out.append("CATEGORIES=" + pkgpath.split("/")[0])
    for k in ("LICENSE", "MACHINE_ARCH", "OPSYS", "OS_VERSION", "PKGPATH",
              "PKGTOOLS_VERSION", "HOMEPAGE"):
        if info.get(k):
            out.append(f"{k}={info[k]}")
    out.append("FILE_NAME=" + os.path.basename(path))
    out.append(f"FILE_SIZE={st.st_size}")
    for line in meta.get("+DESC", "").rstrip("\n").splitlines():
        out.append("DESCRIPTION=" + line)
    return "\n".join(out) + "\n\n"


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: pkg-summary.py каталог-репозитория")
    repo = sys.argv[1]
    pkgs = sorted(f for f in os.listdir(repo) if f.endswith(".tgz"))
    text = "".join(entry(os.path.join(repo, f)) for f in pkgs).encode()
    # Сначала во временный файл: pkgin, пришедший посреди записи, получил
    # бы обрезанный поток и решил, что репозиторий испорчен.
    for ext, opener in (("gz", gzip.open), ("bz2", bz2.open)):
        dst = os.path.join(repo, "pkg_summary." + ext)
        with opener(dst + ".tmp", "wb") as f:
            f.write(text)
        os.replace(dst + ".tmp", dst)
    # pkgin берёт первое найденное из zst, xz, bz2, gz: сводка другого
    # сжатия, оставшаяся от прежней публикации, заслонила бы новую.
    for ext in ("zst", "xz"):
        stale = os.path.join(repo, "pkg_summary." + ext)
        if os.path.exists(stale):
            os.remove(stale)
    print(f"{repo}/pkg_summary.{{gz,bz2}}: {len(pkgs)} пакетов")


if __name__ == "__main__":
    main()
