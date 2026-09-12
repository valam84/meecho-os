# Contributing

**You do not need the board.** Most of the open work — the file system, the
servers, the userland, the test benches, the VM — is reachable from QEMU, and
QEMU is one `apt install` away. The board matters for one category of change,
and this document is mostly about telling that category apart from the rest.

## Getting to a running system

```bash
git clone -b aarch64 <this repository> ~/minix-src
cd ~/minix-src
bash port/build-all.sh          # about two hours, most of it a compiler
bash port/ramimage.sh -d        # boots, root on a virtio-blk disk
```

Details and traps: [docs/meecho/BUILDING.md](docs/meecho/BUILDING.md).
If you only want to look at a running system first, take a release and run it —
[docs/meecho/RUNNING-QEMU.md](docs/meecho/RUNNING-QEMU.md).

## Work that is ready to be picked up

### Small, and reachable from QEMU

- **Directories that do not build yet.** Of 246 in `lib`, `bin`, `sbin`,
  `usr.bin`, `usr.sbin` and `libexec`, 202 build. `bash port/tree-survey.sh`
  reports which do not and why. When the code is ours, fix it properly — add
  the missing include, declare the prototype. When it is a third-party import
  under `external/`, use NetBSD's own mechanism: one named `CC_WNO_*` in the
  Makefile beside the import, for one file, with a comment saying which
  diagnostic and why. Most failures are one of two things: a genuine LP64 bug,
  or a diagnostic GCC 15 turned into an error.
- **Freshening a userland program from NetBSD-current.** The upstream in this
  tree is 2015; NetBSD-current has both a newer upstream *and* the same build
  system *and* the fixes for modern compilers. `port/import-netbsd.sh` does the
  move. Before importing, diff your copy against the 2015 NetBSD base: if MINIX
  never patched it, the replacement is safe. This is how `awk` went from 2012 to
  2026, `less` from 458 to 643, `bzip2` from 1.0.6 to 1.0.8.
### Medium

- **Move the userland onto shared libraries.** Dynamic linking itself works as
  of 2026-09-10 — a program with a `PT_INTERP` starts through `ld.elf_so`, and
  `dlopen` works — but `LDSTATIC` in `share/mk/bsd.own.mk` is still `-static`,
  so every program in the system carries its own copy of libc. What is left is
  TLS (the branches in `ld.elf_so` are fenced off, as on arm) and
  `MKPICINSTALL`.
- **Raise the 4 GB physical memory ceiling.** It comes from the free-page bitmap
  in `servers/vm/alloc.c` and `VM_MAX_PHYS_MEM` beside it. The kernel does not
  need the limit at all.
- **The MINIX test suite.** It builds and runs now — 112 programs, through
  `port/test/suite/qemu-tests.py` — and the failures that are left are real bug
  reports waiting for somebody to read them.
- **Another AArch64 board.** Every driver already finds its hardware in the
  device tree and every permission is granted from a `compatible` string, so a
  new board is mostly a question of which drivers it needs, not of a new board
  file. A Raspberry Pi 4 draft BSP is in `port/bsp/broadcom/` — written,
  never compiled, and now out of the way of the main target.

### Large, and needs the board

- ~~**Stage 9: DMA and interrupts for the eMMC driver.**~~ Done 2026-09-09: the
  driver transfers by ADMA2 and waits on an interrupt. What is left of it is the
  bounce buffer that still cuts every request into 32 KiB pieces.
- **A driver for the SD card controller** (`rockchip,rk3568-dw-mshc`).
  Different registers from the eMMC's SDHCI, and there is no driver at all.
- ~~**USB.** Nothing exists.~~ It exists as of 2026-09-11: an xHCI driver, the
  tree's own hub and mass storage drivers over a URB layer, and a flash drive
  behind the board's hub read at 15–16 MB/s. Three pieces are open, and each is
  a real task: nothing starts the stack at boot; chained transfer descriptors
  have a reproducible failure nobody has explained, so a transfer is kept
  inside 64 KiB; and the 6 MB/s between us and the vendor kernel is measured to
  be in `usb_storage`'s DDEKit thread hand-offs, not in the controller.

### Investigations, where the honest answer is "nobody knows"

- **A chained USB transfer descriptor fails on this controller**, and only
  after a particular shape of request. Every transfer before it is complete and
  correct; then a status block comes back with the wrong tag and the device is
  wedged. Not the copying, not the cache range, not the chunk size — all three
  were tried and ruled out. Under debug logging it does not happen, which says
  it is a race and says nothing about where.
- **VFS panics with "process has two calls"** when a reboot arrives in the
  middle of a block transfer, and takes PM down with it.
- ~~**SMP occupancy tops out below two cores of four.**~~ Closed 2026-09-09:
  there was no ceiling, the benchmark was measuring message passing rather than
  computation.
- ~~**`trace(1)` fails with "Kernel magic check failed".**~~ Closed 2026-09-09,
  and the cause was not in the code at all: an object file had been compiled
  before the LP64 fix and carried the truncation already generated.

## How a change is proven

In this order, and each stage answers a different question:

**1. A host bench, where one exists.** `port/test/` holds benches that compile
the real code with the machine stubbed out: `cachectl` (4.66 million checks over
cache-line arithmetic), `sdmmc` (133 checks, the card layer through its function
table and the block layer against a grant model), `xhci` (118 checks against a
model of the controller — memory with no coherency and a part that reads a ring
only when the doorbell rings), `dwmac` (the pin arithmetic against a table
computed by hand, separately, first), `uds`, `hashbang.sh`. They run in seconds
and they catch broken expectations immediately.

A bench that passes is worth nothing until it has been shown to fail, so the
xHCI one ships with `mutate.sh`: it puts each of the driver's nine real defects
back into a copy, one at a time, and every one of them must be caught. A
mutation that survives is a check that is decoration — and one did survive,
which is how a defect that had been fixed in one file and repeated in another
was found.

They also have a known blind spot, and it is worth stating: **a bench cannot
catch a misunderstanding of the hardware.** Of the four things the SDHCI
controller needed that are not in the specification, the bench had access to
none. What it did instead was keep the card layer correct while attention was
on the hardware, which is real value but a different kind.

**2. QEMU.** Correctness of logic, all four combinations of entry level and GIC
version, several core counts. A driver whose hardware QEMU does not have must
still be run there — the test is that it says so and the system boots on.

**3. The board**, for anything QEMU cannot check.

> ### The rule this port keeps repeating
>
> **QEMU checks logic. It does not check assumptions about hardware.**
> The instruction cache is not coherent with the data cache and TCG hides it.
> TCG's software TLB is not tagged, so an ASID scheme can look correct without
> the emulator ever showing why it exists. Page memory attributes are ignored
> entirely, so the wrong memory type is invisible.
>
> Code that touches caches, TLBs or memory attributes is written from the
> architecture manual and **counted as unproven until a board runs it.** Say so
> in the commit message.

Seven fixes were needed to reach a prompt on the board and **not one of them
reproduces on QEMU.** That is the single most useful thing to know about this
project.

And the corollary, which cost four board runs: **"it does not reproduce on the
emulator" only means something if the emulator is running the same thing.**
An sshd started by hand with `-E file -o LogLevel=DEBUG3` is not the sshd that
`rc` starts, and only the second one failed.

## Committing

Work happens on the `aarch64` branch. Commit as you go rather than accumulating.

**A commit message answers *why*, not *what*.** In a year it will not be obvious
why `bsd.own.mk` grew an aarch64 branch. The diff says what changed; you are the
only one who can say what it was for.

Changes fall into three kinds, and saying which one helps a great deal:

1. **Age of the code** — does not build with a modern compiler. Nothing to do
   with the port; broken the same way for every architecture.
2. **An aarch64 gap** — a place that does not know about the architecture
   because nobody ever built it. This is the actual work of the port.
3. **Fork identity** — the rename MINIX → MEECHO. Fixes nothing and ports
   nothing; it changes what a person sees. The line runs through *who reads the
   string*: the banner, `motd`, `OS_NAME` change; `__minix`, the triple
   `aarch64-elf64-minix` and `*-minix` in `config.sub` do not, because
   third-party software recognises the system by those and renaming them would
   break it silently, by compiling the wrong branch.

Anything non-obvious also goes into [`port/PORTING-LOG.md`](port/PORTING-LOG.md).
That file is in Russian and its entries are long, because a short entry is
usually the "what" again. If you would rather write in English, do — a mixed
log is much better than a missing entry.

**Record the wrong explanations too.** There is an entry called "a wrong guess"
about a memory-corruption story that fit every symptom, coincided with the fix,
and was false. A plausible story that coincided with a fix is not an analysis.

## Style

Match the code around you: MINIX and NetBSD conventions, tabs, comments that
explain why. The kernel is C, compiled `-mgeneral-regs-only`; it must not use FP
registers, because nothing saves them on the way into an exception.

Fix our code properly and reach for compatibility flags only for the host
toolchain, which does not ship in the OS. The three-way boundary — our code,
host tools, third-party imports under `external/` — is drawn by **authorship**,
not by how hard the fix would be.

## Reporting something

Issues are welcome, including "I could not build it" — that is a documentation
bug and it is worth fixing. For a failure on the machine, the useful things are:
the boot arguments, whether it was QEMU or the board, which of the four
QEMU combinations, and the console output from the very beginning. On the board
the beginning is only in the log if the console was listening **before** the
reboot.

If an entry in the porting log matters to you and you cannot read Russian, ask.
That is a reasonable request and it will be answered.
