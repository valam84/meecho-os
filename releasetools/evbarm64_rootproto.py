#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Собрать proto для корня на диске: дерево ramdisk плюс userland из DESTDIR.

Ramdisk - это система до тех пор, пока не появится корень, на который можно
переключиться; в нём лежит ровно то, без чего этого не сделать. Корень на
eMMC - другое дело: там есть место, и туда должно попасть всё, что дерево
собрало и поставило в DESTDIR.

Взять одно только DESTDIR нельзя: узлы устройств, /etc и загрузочные серверы
описаны не там, а в proto ramdisk, и описаны буквально - с правами, с
владельцами, с major/minor. Поэтому proto ramdisk остаётся основой, а из
DESTDIR добавляются каталоги userland. Там, где имя есть и там и там,
выигрывает ramdisk: это его /etc и его серверы поднимают систему.

    evbarm64_rootproto.py <proto.gen> <каталог-ramdisk> <DESTDIR> [подкаталог...]

Пути к файлам в готовом proto - абсолютные, поэтому mkfs.mfs можно звать из
любого каталога.
"""

import os
import stat
import sys

# Что берётся из DESTDIR. usr/lib и usr/include не берутся: всё слинковано
# статически, а заголовки и архивы библиотек на плате не нужны никому, кроме
# компилятора, которого там нет.
#
# service и etc/system.conf.d - это то, что запускается уже после того, как
# корень поднялся: сетевой стек и его драйвер. Загрузочные серверы описаны в
# proto ramdisk и по имени выигрывают у одноимённых из DESTDIR, так что здесь
# добавляются только те, которых там нет. minix-service ищет описание службы
# в /etc/system.conf.d/<метка>, поэтому каталог идёт следом за самими
# двоичными файлами.
DEFAULT_SUBDIRS = [
    "bin",
    "service",
    "etc/system.conf.d",
    "sbin",
    "usr/bin",
    "usr/sbin",
    "usr/libexec",
    "usr/man",
    "usr/share",
]


class Node(object):
    """Каталог (kids != None) или лист (line - хвост строки proto)."""

    def __init__(self, line=None):
        self.line = line
        self.kids = None if line is not None else {}
        self.order = []

    def child(self, name):
        if name not in self.kids:
            self.kids[name] = Node()
            self.order.append(name)
        return self.kids[name]

    def add(self, name, node):
        if name not in self.kids:
            self.order.append(name)
            self.kids[name] = node


# --------------------------------------------------------------- разбор proto

def parse_proto(path):
    """Прочитать proto и вернуть (шапка, корневой Node)."""
    with open(path, "r") as f:
        raw = f.read()
    lines = raw.split("\n")
    header = lines[:2]                  # имя загрузочного блока и "inodes blocks"
    toks = []
    for ln in lines[2:]:
        toks.extend(ln.split())
    pos = [0]

    def take():
        t = toks[pos[0]]
        pos[0] += 1
        return t

    def parse_dir(node):
        while True:
            name = take()
            if name == "$":
                return
            node.add(name, parse_entry(take()))

    def parse_entry(mode):
        uid, gid = take(), take()
        if mode[0] == "d":
            n = Node()
            n.line = "%s %s %s" % (mode, uid, gid)
            n.kids = {}
            parse_dir(n)
            return n
        if mode[0] in "bc":
            return Node("%s %s %s %s %s" % (mode, uid, gid, take(), take()))
        # обычный файл или символическая ссылка: одно поле следом
        return Node("%s %s %s %s" % (mode, uid, gid, take()))

    root = Node()
    root.line = None
    root.kids = {}
    root_mode = take()
    root_uid, root_gid = take(), take()
    root.line = "%s %s %s" % (root_mode, root_uid, root_gid)
    parse_dir(root)
    return header, root


def absolutise(node, base):
    """Пути к файлам в proto ramdisk даны относительно его каталога."""
    if node.kids is None:
        parts = node.line.split()
        if parts[0][0] == "-" and not parts[3].startswith("/"):
            parts[3] = os.path.join(base, parts[3])
            node.line = " ".join(parts)
        return
    for name in node.order:
        absolutise(node.kids[name], base)


# ------------------------------------------------------------- обход DESTDIR

def mode_of(st):
    return "%03o" % (st.st_mode & 0o777)


def add_tree(parent, src):
    """Добавить содержимое каталога src под узел parent."""
    for name in sorted(os.listdir(src)):
        path = os.path.join(src, name)
        st = os.lstat(path)
        if stat.S_ISLNK(st.st_mode):
            parent.add(name, Node("s--%s 0 0 %s"
                                  % (mode_of(st), os.readlink(path))))
        elif stat.S_ISDIR(st.st_mode):
            if name in parent.kids and parent.kids[name].kids is not None:
                sub = parent.kids[name]          # каталог уже есть - дополняем
            else:
                sub = Node()
                sub.line = "d--%s 0 0" % mode_of(st)
                sub.kids = {}
                parent.add(name, sub)
            add_tree(sub, path)
        elif stat.S_ISREG(st.st_mode):
            if name in parent.kids:
                continue                         # ramdisk уже сказал своё
            parent.add(name, Node("---%s 0 0 %s" % (mode_of(st), path)))
        # всё прочее в DESTDIR не встречается и молча пропускается


def descend(root, relpath):
    """Найти или создать каталог по пути вида usr/bin."""
    node = root
    for part in relpath.split("/"):
        if part not in node.kids:
            sub = Node()
            sub.line = "d--755 0 0"
            sub.kids = {}
            node.add(part, sub)
        node = node.kids[part]
        if node.kids is None:
            sys.exit("%s: на пути стоит файл, а не каталог" % relpath)
    return node


# ------------------------------------------------------------------- запись

def emit(node, out, depth=0):
    pad = "\t" * depth
    for name in node.order:
        kid = node.kids[name]
        if kid.kids is None:
            out.write("%s%s %s\n" % (pad, name, kid.line))
        else:
            out.write("%s%s %s\n" % (pad, name, kid.line))
            emit(kid, out, depth + 1)
            out.write("%s$\n" % pad)


def main(argv):
    if len(argv) < 4:
        sys.exit(__doc__)
    proto, ramdisk_obj, destdir = argv[1], argv[2], argv[3]
    subdirs = argv[4:] or DEFAULT_SUBDIRS

    header, root = parse_proto(proto)
    absolutise(root, ramdisk_obj)

    for sub in subdirs:
        src = os.path.join(destdir, sub)
        if not os.path.isdir(src):
            sys.stderr.write("нет %s, пропущено\n" % src)
            continue
        add_tree(descend(root, sub), src)

    out = sys.stdout
    out.write("\n".join(header) + "\n")
    out.write("%s\n" % root.line)
    emit(root, out, 1)
    out.write("$\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
