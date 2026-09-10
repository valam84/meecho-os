<p align="center">
  <img src="docs/meecho/branding/meecho_lockup_horizontal.svg" alt="MEECHO" width="420">
</p>

<p align="center">
  <b>A microkernel operating system for AArch64, forked from MINIX 3.</b><br>
  Boots on QEMU and on a real board. Everything below the C library was written for this port.
</p>

<p align="center">
  <a href="#state-of-the-system">State</a> ·
  <a href="#try-it-in-five-minutes">Try it</a> ·
  <a href="docs/meecho/BUILDING.md">Build</a> ·
  <a href="docs/meecho/ARCHITECTURE.md">Architecture</a> ·
  <a href="CONTRIBUTING.md">Contributing</a> ·
  <a href="README.ru.md">По-русски</a>
</p>

<p align="center">
  <a href="https://meecho.ru"><b>meecho.ru</b></a> ·
  <a href="https://github.com/valam84/meecho-os/releases/latest">Releases</a> ·
  <a href="https://github.com/valam84/meecho-os/issues">Open work</a>
</p>

---

```
  |\/|       __  __ ___ ___ ___ _  _  ___
==<-->==    |  \/  | __| __/ __| || |/ _ \
            | |\/| | _|| _| (__| __ | (_) |
            |_|  |_|___|___\___|_||_|\___/

MEECHO/aarch64: 4 cores, memory 0000000040000000-0000000080000000
eMMC "PJ3032": 61079552 sectors, 8-bit bus at 50000000 Hz, high speed
phy at 0: id 0x4f51e91b (Motorcomm YT8531), link up: 100 Mbit/s, full duplex
Started VFS: 9 worker thread(s)
Root device name is c0d0
/dev/c0d0: clean

MEECHO/evbarm64 0.1.0 (console)

login:
```

## What this is

MINIX 3 is a microkernel operating system: the kernel does scheduling, memory
translation and message passing, and *everything else* — file systems, drivers,
the network stack, the process manager — runs as an ordinary user process that
can crash and be restarted. It is one of the few such systems that ever became
a usable Unix rather than a paper design.

Upstream MINIX 3 stopped moving in 2018, and it never ran on 64-bit ARM.
MEECHO is a fork that does. It runs on QEMU's `virt` machine and on a
**BIGTREETECH CB2** — a Rockchip RK3566 board with four Cortex-A55 cores — where
it boots from on-board eMMC, brings up Ethernet, and accepts ssh logins.

This is not a rewrite. The servers, the C library and the NetBSD-derived
userland come from MINIX; what was written for this port is everything that
touches the machine:

- the kernel's architecture layer — MMU with four levels of translation,
  exception vectors, context switching, ASID-tagged address spaces, FP/SIMD,
  the generic timer, SMP through PSCI, spinlocks;
- interrupt controllers — GICv2 **and** GICv3, chosen at boot from the device
  tree, because the target board has one and QEMU can give either;
- a 64-bit message ABI (the IPC message was 64 bytes and had to be re-laid out
  for LP64 — all 256 variants, by a generator);
- drivers: SDHCI eMMC, DesignWare GMAC Ethernet with a Motorcomm PHY,
  DesignWare 8250 and PL011 serial, PL031 RTC, the SoC's hardware RNG,
  virtio-mmio block and network;
- an on-disk file system, **MFS V4**, with 64-bit sizes, nanosecond timestamps,
  variable-length directory entries, a metadata journal, and an `fsck` that was
  tested by corrupting real volumes.

**No board tables.** Every driver finds its hardware in the device tree, and
the permission to touch a register range is granted by RS from a
`devicetree "compatible";` line in `system.conf` — the same job the PCI server
does on x86.

## State of the system

Everything in this table has been run. "Board" means a BIGTREETECH CB2 on a
desk, not a simulation of one.

| | QEMU `virt` | CB2 board |
|---|---|---|
| Boots to `login:`, shell responds | yes | yes |
| SMP, work spread over 4 cores | yes (1.78× for 4 jobs) | yes (1.6× vs. one core) |
| Root file system on disk, survives power-off | yes (virtio-blk) | yes (eMMC, `dd`-written image) |
| Journal replay and `fsck` after corruption | yes | yes |
| Userland: 276 programs, 210 man pages | yes | yes |
| `vi`, `less`, `man`, `make`, `awk`, `sed`, `tar`, `grep` | yes | yes |
| Network: ping, TCP to a real server | yes (virtio-net) | yes (GMAC + YT8531 PHY) |
| DHCP, name resolution | yes | yes |
| ssh in, interactive session on a pty | yes | yes |
| Real-time clock | yes (PL031) | no RTC; date comes from DHCP-era `rc` |
| Reboot and power-off through PSCI | yes | yes |

Known gaps, stated plainly:

- ~~**Storage is slow on purpose.**~~ Fixed 2026-09-09: the eMMC driver
  transfers by ADMA2 and waits on an interrupt. Reading is about 2.4× faster,
  writing about twice, unpacking an archive about 1.5× — measured on the board
  by the same script before and after, see
  [milestone 9](docs/meecho/ROADMAP.md). A request is still cut into 32 KiB
  pieces, which is the size of the bounce buffer rather than a limit of the
  controller.
- **No USB, no display, no audio.** The board's console is a serial port.
- **Everything is statically linked.** `ld.elf_so` links for aarch64 now, but
  `exec` has no `PT_INTERP` path yet.
- **No SD-card controller driver.** The card slot is a DesignWare mobile
  storage host — different registers from the eMMC's SDHCI, different file.

## Try it in five minutes

Download a release from [the releases page][rel] (or from [meecho.ru][site]),
unpack it, and run QEMU. Nothing is built, nothing is installed:

```bash
tar xf meecho-qemu-0.1.0.tar.gz
cd meecho-qemu-0.1.0
./run.sh
```

Log in as `root`, no password. `poweroff` when you are done. The script is four
lines; if you would rather type them yourself, see
[docs/meecho/RUNNING-QEMU.md](docs/meecho/RUNNING-QEMU.md), which also covers
booting at EL2, GICv3, several cores, and reaching the guest's sshd from the
host.

For the board, [docs/meecho/RUNNING-CB2.md](docs/meecho/RUNNING-CB2.md)
describes the SD-card kit: it boots MEECHO **once**, and the next power cycle
returns the vendor Linux, so a board you cannot physically reach is not a board
you can lose.

Building from source takes a couple of hours, most of it a cross-toolchain:
[docs/meecho/BUILDING.md](docs/meecho/BUILDING.md).

## The porting log

Every non-obvious decision in this tree is written down in
[`port/PORTING-LOG.md`](port/PORTING-LOG.md) — 9,400 lines of it — and each
entry answers *why*, not *what*. Some of it is worth reading even if you never
build the system:

- Why the instruction cache made `exec` load a program that ran the *previous*
  owner's code, and why QEMU can never show you that.
- Why an SDHCI controller that follows the specification still needs four
  undocumented things before a card answers, and how each of them fails.
- Why `select(2)` reported an empty pipe as readable, why it only broke the
  *second* ssh login, and why raising the log level "fixed" it.
- Why a `#!` script crashed depending on the length of the environment.
- Why every `longjmp(3)` returned to address zero, why the check meant to
  catch exactly that rejected valid buffers instead, and why all of it
  surfaced the first evening the test suite was ever run.
- Why the reference for a driver is the vendor kernel *that is on the board*,
  not the newest code for the same chip.

**It is in Russian**, and translating 900 KB of it would produce a translation
that starts drifting from the original the same week. The code, the comments,
the commit messages and all of this documentation are in English. If a
particular entry matters to you and you cannot read it, open an issue and ask —
that is a reasonable request and it will be answered.

## Layout

| Path | What is there |
|---|---|
| `minix/kernel/arch/aarch64/` | the architecture layer: MMU, traps, timer, SMP, GIC, PSCI |
| `minix/kernel/arch/aarch64/bringup/` | the standalone bring-up kernel this was grown from; still runs |
| `minix/servers/vm/arch/aarch64/` | four-level page tables |
| `minix/drivers/` | `sdmmc` (eMMC), `dwmac` (Ethernet), `tty`, `random/trng`, virtio |
| `minix/fs/mfs/`, `minix/lib/libminixfs/` | MFS V3 and V4, the journal |
| `port/` | the scripts that build, run and debug all of this |
| `port/test/` | host-side test benches: cache maintenance, the card layer, pin arithmetic, uds |
| `minix/tests/` | the MINIX test suite, 112 programs; `port/test/suite/` runs it on QEMU |
| `docs/meecho/` | this documentation |
| `PLAN.md`, `port/PORTING-LOG.md` | the roadmap and the log (Russian) |

## Contributing

There is a lot of room, and the interesting parts are not glamorous. Start at
[CONTRIBUTING.md](CONTRIBUTING.md) — it lists tasks that are actually ready to
be picked up, explains how a change is proven (host bench, QEMU, board — in
that order), and states the one rule this port keeps repeating:

> **QEMU does not check caches, TLBs or memory attributes.** Code that touches
> them is written from the architecture manual and counted as unproven until a
> board runs it.

You do not need the board to be useful. Most of the open work — the file
system, the servers, the userland, the test benches — is reachable from QEMU,
and QEMU is one `apt install` away.

## Licence and lineage

BSD 3-Clause, from MINIX 3 and NetBSD; see [LICENSE](LICENSE). Upstream is
[Stichting-MINIX-Research-Foundation/minix](https://github.com/Stichting-MINIX-Research-Foundation/minix),
and this fork keeps its full history — `git log` reaches back to the original
tree. The port lives on the `aarch64` branch.

The platform identity stays `minix`: the predefined `__minix`, the target
triple `aarch64-elf64-minix`, `*-minix` in `config.sub`. Third-party software
recognises the system by those, and renaming them would break it silently, by
compiling the wrong branch. What changed is the name a *person* sees.

[rel]: https://github.com/valam84/meecho-os/releases/latest
[site]: https://meecho.ru
