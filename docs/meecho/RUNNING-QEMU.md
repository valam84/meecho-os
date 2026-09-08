# Running MEECHO on QEMU

## From a release

```bash
tar xf meecho-qemu-0.1.0.tar.gz
cd meecho-qemu-0.1.0
./run.sh
```

`run.sh` needs `qemu-system-aarch64` and nothing else. Log in as `root`, no
password. `poweroff` shuts the machine down and QEMU exits on its own.

The archive holds three files: `kernel.bin` (a flat image with an arm64 header),
`boot.mba` (the boot archive with the twelve boot images inside), and
`disk.img.gz` — a 512 MB MFS V4 root with the full userland, which `run.sh`
unpacks on first use and then keeps. Anything you write to it stays there,
which is the point: booting twice and finding your file is how you tell a disk
from a ramdisk.

## The command, if you would rather type it

```bash
qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512 -smp 1 \
    -display none -serial stdio \
    -netdev user,id=net0 -device virtio-net-device,netdev=net0 \
    -drive if=none,id=hd0,file=disk.img,format=raw,discard=unmap \
    -device virtio-blk-device,drive=hd0 \
    -kernel kernel.bin -initrd boot.mba \
    -append "rootdevname=c0d0 console=tty00"
```

To boot the ramdisk root instead — no disk at all — drop the two `-drive`
lines and use `-append "bootramdisk=1 console=tty00"`.

**Both boot arguments are required, and each fails quietly if left out.**
`bootramdisk=1` tells `/etc/rc` that the root it was handed is the root it
keeps; without it the script looks for a disk it has no driver for.
`console=tty00` points `/dev/console` at the serial line; without it the console
is minor 0, which is the video console this machine does not have, and `open()`
answers `ENXIO`.

**Do not add `hz=`.** The kernel reads it, but `sys_hz()` in libsys reads the
fallback constant beside it, and the two disagreeing is silent.

## From a build

```bash
bash port/ramimage.sh -r          # ramdisk root
bash port/ramimage.sh -d          # root on a virtio-blk disk
bash port/ramimage.sh -b -d       # rebuild the ramdisk first
bash port/ramimage.sh -d -n       # assemble the disk image, do not run
```

The disk image is kept between runs — that *is* the test — and is rebuilt when
the ramdisk is newer or when `DISK_FRESH=1` is set. `DISK_MB` sets its size,
512 by default.

## The four combinations that need checking

The entry exception level, the interrupt controller version and the number of
cores are independent, and each has code of its own. A mistake shows up in one
combination and not the others, so all of them are worth a run:

```bash
bash port/ramimage.sh -r          # EL1, GICv2, 1 core
bash port/ramimage.sh -2          # EL2, as U-Boot leaves a kernel on a board
bash port/ramimage.sh -3          # GICv3, as the target board has
QEMU_SMP=4 bash port/ramimage.sh -2 -3
```

The drop from EL2 in `head.S` is what enables the GICv3 system registers for
EL1, which is why `-2 -3` is a different test from either flag alone.

## Driving it without a terminal

QEMU reads the serial line from stdin, so a scripted run is a pipeline with
sleeps. `QEMU_TIMEOUT` is not optional: the kernel parks on `wfi` and QEMU will
not exit by itself.

```bash
{ sleep 18; echo root; sleep 3; echo "echo hello > /root/f; cat /root/f";
  sleep 3; echo /sbin/poweroff; sleep 6; } |
QEMU_TIMEOUT=60 bash port/ramimage.sh -d
```

Two things bite here every time. A line longer than about 255 bytes never
reaches the shell — the tty's canonical buffer truncates it silently, so a long
one-liner produces no output at all. And an end-of-command marker written with
`echo M0` appears in the log *before* the command runs, because the terminal
echoes what was typed; write markers as `printf 'd%s\n' 0` instead.

If there is no `# ` prompt after a command, the shell is waiting, not the
system. `bzip2 --version` in particular does not exit: it prints its licence
and then compresses stdin.

## Reaching the guest from the host

`QEMU_HOSTFWD` forwards ports; several rules separated by commas:

```bash
QEMU_HOSTFWD=2222 bash port/ramimage.sh -d          # host 2222 -> guest 22
QEMU_HOSTFWD="2222,8080:80" bash port/ramimage.sh -d
ssh -p 2222 root@127.0.0.1
```

Connecting to `127.0.0.1` *inside* the guest exercises neither the driver nor
lwip, so a test that does that proves nothing about the network. Come in from
the host.

## Debugging

QEMU's gdb stub works and has earned its keep twice:

```bash
qemu-system-aarch64 ... -gdb tcp::1234 -S
gdb-multiarch ~/obj-evbarm64/minix/kernel/kernel -ex 'target remote :1234'
```

The kernel is linked at its own virtual addresses, so symbols resolve as they
are. For types as well as symbols, rebuild with debug info — the code
generation does not change, DWARF sections are added:

```bash
nbmake-evbarm64-el -C minix/kernel cleandir
nbmake-evbarm64-el -C minix/kernel obj
nbmake-evbarm64-el -C minix/kernel -j24 DBG="-O0 -g" dependall
```

Without it gdb answers `'proc' has unknown type`. The most useful dump when
the system hangs is the process table: if the program counter sits in
`halt_cpu`, everything is blocked, and `p_rts_flags`, `p_getfrom_e` and
`p_sendto_e` say who is waiting for whom.

A hardware watchpoint fires on a *change* of value, so the first hits are
legitimate ones and have to be stepped over by a `commands` block that counts
and continues.

The second tool is `DEBUG_DUMPIPC` in `minix/kernel/debug.h`: set it to 1,
rebuild the kernel, and every message goes to the console with sender,
receiver and type spelled out. That is what showed `ENXIO` coming back from
`tty` on opening `/dev/console`. It is noisy — 8,000 lines in 40 seconds, and
what you want is at the end. **Set it back to 0 afterwards.**

## Checking that it really works

Reaching a prompt does not prove the timer ticks; you can get there on messages
alone. Read `/proc/uptime` twice with a pause.

Four cores booting does not prove work is being spread over them either. The
only meaningful comparison is the same machine with `no_smp` — one counting
loop against four at once. On QEMU four cost 1.78× one, not 4×.

```bash
cat /proc/cpuinfo                # how many cores the system sees
sh port/test/smp/smpload.sh      # timings
sh port/test/smp/smpocc.sh       # how busy the cores are
```
