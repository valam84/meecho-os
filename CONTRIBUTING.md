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
- **The MINIX test suite.** `tests/` has never been run on this port. Finding
  out what passes is itself a contribution, and every failure is a real bug
  report.
- **Another AArch64 board.** Every driver already finds its hardware in the
  device tree and every permission is granted from a `compatible` string, so a
  new board is mostly a question of which drivers it needs, not of a new board
  file. A Raspberry Pi 4 draft BSP is in `port/bsp/broadcom/` — written,
  never compiled, and now out of the way of the main target.

### Large, and needs the board

- **Stage 9: DMA and interrupts for the eMMC driver.** This is the best next
  piece of work in the project and it is described in
  [docs/meecho/ROADMAP.md](docs/meecho/ROADMAP.md). Both of the driver's
  limitations were deliberate and neither is justified any more.
- **A driver for the SD card controller** (`rockchip,rk3568-dw-mshc`).
  Different registers from the eMMC's SDHCI, and there is no driver at all.
- **USB.** Nothing exists. It is a project, not a task.

### Investigations, where the honest answer is "nobody knows"

- **SMP occupancy tops out below two cores of four**, and adding jobs does not
  raise it. A candidate is named in the porting log and not one line of it is
  confirmed.
- **`trace(1)` fails with "Kernel magic check failed".** A real LP64 bug was
  found and fixed on the way there, and the symptom survived it.

## How a change is proven

In this order, and each stage answers a different question:

**1. A host bench, where one exists.** `port/test/` holds benches that compile
the real code with the machine stubbed out: `cachectl` (4.66 million checks over
cache-line arithmetic), `sdmmc` (119 checks, the card layer through its function
table and the block layer against a grant model), `dwmac` (the pin arithmetic
against a table computed by hand, separately, first), `uds`, `hashbang.sh`.
They run in seconds and they catch broken expectations immediately.

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
