# Building MEECHO from source

Two hours on a fast machine, most of it a cross-compiler that is built once.
If you only want to *run* the system, take a release instead — see
[RUNNING-QEMU.md](RUNNING-QEMU.md).

## What you need

A Linux host with a real Unix file system. This was developed on **Ubuntu
26.04** with **GCC 15.2**, under WSL2 and on plain Linux; anything with a GCC
of that generation will do, and older ones are fine for the host tools but not
for building the cross-compiler from the distribution's source packages.

> **The tree must not live on an NTFS drive.** Some names in the NetBSD part of
> the tree are illegal on Windows, and a checkout under `/mnt/c` or `/mnt/d`
> loses them silently — the failure appears much later, as a header that does
> not exist. `build-all.sh` refuses to start there. Clone under `$HOME`.

Disk: about 4 GB for the toolchain, objects and DESTDIR.

```bash
sudo apt install -y \
    build-essential zlib1g-dev libssl-dev bison flex git curl ca-certificates \
    file bc rsync python3 texinfo \
    gcc-aarch64-linux-gnu \
    qemu-system-arm gdb-multiarch device-tree-compiler u-boot-tools \
    dosfstools mtools parted fdisk gdisk
```

For the cross toolchain, the distribution's own compiler sources plus what they
need to build:

```bash
sudo apt install -y gcc-15-source binutils-source \
    libgmp-dev libmpfr-dev libmpc-dev libisl-dev libzstd-dev
```

On WSL, [`port/setup-wsl.sh`](../../port/setup-wsl.sh) does all of the above,
creates an unprivileged build user and puts the tree on ext4. Run it once, as
root.

## Build it

```bash
git clone -b aarch64 <this repository> ~/minix-src
cd ~/minix-src
bash port/build-all.sh
```

That is the whole thing. It runs seven stages in order and each is idempotent,
so an interrupted build is resumed by running it again. To redo just one:

```bash
bash port/build-all.sh dirs image      # after touching a driver
bash port/build-all.sh kernel image    # after touching the kernel
```

| Stage | What it makes | Where |
|---|---|---|
| `tools` | NetBSD host tools — `nbmake-evbarm64-el` and friends | `~/tools-evbarm64` |
| `xtools` | the cross toolchain for `aarch64-elf64-minix` | `~/xtools-aarch64` |
| `includes` | the DESTDIR directory tree and all headers | `~/dest-evbarm64` |
| `libs` | `crt0`, `libc` | `~/dest-evbarm64/usr/lib` |
| `dirs` | 386 directories: libraries, servers, drivers, userland | `~/dest-evbarm64` |
| `kernel` | the kernel ELF | `~/obj-evbarm64/minix/kernel` |
| `image` | `kernel.bin`, `boot.mba` | `~/obj-evbarm64/work` |
| `disk` | a virtio-blk root image for QEMU | `~/obj-evbarm64/work/disk.img` |

The list of directories the `dirs` stage walks is
[`port/build-list.txt`](../../port/build-list.txt), and it is a fact about the
state of the port rather than a preference: of the 246 directories in `lib`,
`bin`, `sbin`, `usr.bin`, `usr.sbin` and `libexec`, **202 build**. The
remaining 44 are the network utilities nothing has needed yet, file systems
this port does not have, and host-only tools. `port/tree-survey.sh` walks the
whole tree and reports which is which, if you want to pick one up.

## Two toolchains, and why

**The kernel** is freestanding and is built by whatever `aarch64` cross-GCC the
distribution ships — `gcc-aarch64-linux-gnu`. It needs no libc and no target
support.

**The userland** cannot use that one: it has to know the target
`aarch64-elf64-minix`, and the compilers in the tree (binutils 2.23.2 from 2013,
GCC 4.8.5 from 2015) neither know it nor build under a modern host GCC.
`port/build-xtools.sh` therefore builds binutils 2.46 and GCC 15.2 from the
distribution's source packages with the patches in
[`port/toolchain/`](../../port/toolchain/). The patches are small — a target
description in `gcc/config`, an emulation in `ld`, and one `|| defined(__minix)`
in GCC's own `stddef.h` without which nothing defines `size_t`. The tree picks
the result up through `EXTERNAL_TOOLCHAIN`.

## Traps that cost real time

**Check `$?`, not the output.** A pipeline like
`nbmake ... | grep -iE "error|warning"` swallows the link line: the build fails,
the image script picks up the *previous* `kernel.bin`, and you end up debugging
a failure that is not in the sources. Every wrapper in `port/` checks status
itself for this reason.

**A header edited in the tree is not seen by the servers until
`tree-includes.sh` runs.** The kernel compiles with `-I` into the source
directory; everything else reads the same header out of DESTDIR. Skipping the
sync means building the kernel and VM against two different definitions of the
same thing — which, for the `MAIR` indices, produced a `SIGBUS` in `rs` on an
unaligned access. The symptom is worse than "nothing changed": it is half
changed.

**Object files outlive their sources.** After replacing a directory wholesale
(an import from NetBSD, say), `cleandir` may not be enough — a stale `.o` with a
newer timestamp survives, and the link fails on a symbol you can see in the
file with your own eyes. `rm -rf ~/obj-evbarm64/<path>`.

**`fixincludes` can shadow the tree's headers.** GCC's build copies "fixed"
versions of `stdlib.h`, `stdio.h`, `stddef.h` and `wchar.h` into
`~/xtools-aarch64/lib/gcc/aarch64-elf64-minix/*/include-fixed/`, and that
directory outranks `-I`. Editing such a header in the tree then changes
nothing at all. `build-xtools.sh` configures with `--disable-fixincludes`; if
you built the toolchain some other way, check that the directory holds only
`README` and `sys/`.

## Where things end up

| Directory | Contents |
|---|---|
| `~/minix-src` | the tree, branch `aarch64` |
| `~/obj-evbarm64` | object files; `work/` holds the bootable products |
| `~/dest-evbarm64` | DESTDIR: the installed system |
| `~/tools-evbarm64` | NetBSD host tools |
| `~/xtools-aarch64` | the cross toolchain |

## The bring-up kernel

`minix/kernel/arch/aarch64/bringup/` is the standalone kernel this port was
grown from — MMU, exceptions, a timer, one process at EL0, and nothing else.
It still builds and still runs, and it is by far the fastest way to try an idea
about the architecture layer:

```bash
cd minix/kernel/arch/aarch64/bringup
make -f Makefile.bringup run       # or run-el2, bench, debug, disasm
```

`run-el2` is not optional when you change early boot: plain `-M virt` enters at
EL1 and the EL2→EL1 path in `head.S` is then never executed, while a real board
hands over at EL2.
