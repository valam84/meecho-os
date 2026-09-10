# Roadmap

The full plan, with the reasoning for each decision, is [`PLAN.md`](../../PLAN.md)
(Russian). This is the summary and the state as of **2026-09-10**.

## Done

| Stage | What it delivered |
|---|---|
| 0–3 | environment, the MINIX side of aarch64, the kernel's architecture layer, the LP64 message ABI |
| 4 | the generic kernel links and boots: MMU, exceptions, timer, a process at EL0 |
| 5 | boot archive, ramdisk root, twelve boot images, **a shell prompt** |
| 5.7 | GICv3 alongside GICv2, chosen from the device tree |
| 6 | SMP: four cores through PSCI, per-CPU data, spinlocks |
| 7 | storage: a root on disk, `readclock`, **MFS V4**, a metadata journal, `fsck` |
| 8 | the board as a working machine: eMMC root, userland, network, ssh, SMP under load |
| 9.1–9.3 | storage at the speed of the hardware: ADMA2 and an interrupt for the eMMC |

Stage 8 is closed in full. What that means concretely: a CB2 boots from its own
eMMC into a 1 GB root with 276 programs, brings up Ethernet, gets an address by
DHCP, resolves names, and accepts ssh logins — and four cores under load are
1.6× one core.

**8.1 was stopped rather than finished.** Netbooting needs a U-Boot with
`CONFIG_NET`, and the vendor's has none — 98 commands, not one of them network.
Building our own is the single irreversible change on this board, and the
watchdog does not reach it. The server half is done and tested; the gain would
have been negative anyway (11.4 MB over TFTP is 8.1 s against 1.1 s from the
card), and ssh removed the card from the loop long ago.

## Stage 9: storage at the speed of the hardware — 9.1-9.3 done

The eMMC driver used to be **slow twice over**: data moved through a FIFO with
the processor, and waiting was a poll with a deadline. Both were deliberate,
both had stopped being justified, and both were replaced on 2026-09-09. The
transfer is now ADMA2 and the wait is an interrupt on line 51.

**9.1 — the number first.** Measured before the change and the same way after,
from the board over ssh, by `port/test/sdmmc/bench.sh`:

| | Before | After |
|---|---|---|
| Read `/dev/c0d0`, `bs=64k`, 32 MiB | 6.0-7.2 MiB/s | **14.0-15.8 MiB/s** |
| Read `bs=1m`, 32 MiB | 6.0-6.6 MiB/s | **13.8-18.7 MiB/s** |
| Write 16 MiB and `sync` | 7.9 MiB/s | **15.5-32.0 MiB/s** |
| Unpack `/bin` (10.4 MB) | 3.03 s | **1.74-2.09 s** |
| Boot to `login:` | 24.61 s | 24.12 s — unchanged |

Boot did not move because it is not a storage measurement: the console at
115200 and the network driver's per-tick debug printing cost far more than
reading the images. An intermediate run gave 29.11 s there and nothing accounts
for it but spread — recorded as spread, not as an effect.

The first version of the benchmark measured the cache rather than the card:
three reads of one region gave 6.4, 66.7 and 145.5 MiB/s, because a raw device
read goes through the same block cache a file does. Each run now reads its own
region.

**9.2 — DMA.** ADMA2 with 32-bit descriptors, chosen by the capability
register rather than in advance (`caps = 0x226dc881`: ADMA2 yes, 64-bit bus
no). SDMA is offered by this part too and is refused: a second untested way to
do what the first already does, needing the driver to reprogram the address at
every buffer boundary. The buffer comes from `alloc_contig()` and its physical
address is carried down to the host; zero there means "the processor moves it",
so there are two ceilings, and the four-block one still applies to any buffer
without a physical address. Cache maintenance follows the rule taken from
NetBSD's `bus_dma`, including the second invalidate after a read.

**9.3 — the interrupt.** Only Command Complete and Transfer Complete are
signalled: Buffer Ready is a level that stands for as long as the FIFO has
room, and signalling a level nobody clears is the livelock this port already
met on the UART. Polling stays as the fallback and is reached three ways — no
line in the tree, a line the kernel will not arm, and a line that interrupts
64 times without the status register saying anything new.

**How it was checked.** Digests of three 8 MiB regions of the eMMC were taken
by the vendor kernel's own driver **before** the run and matched what MEECHO
read through ADMA2; 72 MiB written through DMA survived a reboot with the same
md5; `fsck_mfs` said `clean` afterwards. Host benches: 133 checks, 0 failures.
None of this is checkable under QEMU, which models neither caches nor this
controller.

**9.4 — the SD card controller, still open.** `mmc@fe2b0000` is a DesignWare
mobile storage host: different registers, its own clock-update command, no
driver. A file beside `sdmmc_sdhci.c` behind the same host table.

**What is left of 9.2.** The four-block ceiling is gone, but a request is
still cut into 32 KiB pieces — that is `SDMMC_CHUNK_SECTORS`, the size of the
bounce buffer, not a limit of the controller, and each piece still costs two
grant copies.

## The test suite, run for the first time

The MINIX suite in `minix/tests` had never been built on this port. It builds
now — 112 programs — and `port/test/suite/qemu-tests.py` runs it on QEMU, each
test with its own deadline so one hang cannot eat the rest. Getting it to
build took five changes: 26 duplicate tentative definitions (`-fno-common`),
a library that was never installed, three places that only knew about i386 and
arm, and turning off dynamic linking for the suite, since `exec` has no
`PT_INTERP` path and all 112 programs would have died before `main()`.

The first run: **66 passed, 19 failed, 12 hung, 4 not built**. Five fixes
later: **72 passed, 12 failed, 13 hung**, with a written cause for each one
that is left. The full table, both runs side by side, is
`port/test/suite/results-qemu.txt`.

What it found in one evening, after months in which nothing had looked here:

- **Three defects in `setjmp`/`longjmp`**, all in code taken from NetBSD in
  2014 and, by the look of it, never executed. Every `longjmp(3)` returned to
  `TPIDR_EL0` — address zero in a static binary — because both implementations
  reuse the register holding the saved return address for the TLS pointer. The
  check meant to catch a corrupt buffer rejected valid ones instead: AAPCS64
  makes a zero frame pointer the legal end of the frame chain, which is what
  `main()` has under `-fomit-frame-pointer`. And `__siglongjmp14` tested the
  magic bit the wrong way round, so each half of `sigsetjmp` met the other's
  magic. The shell died on every `^C` from this.
- **MFS never said whether it truncates long names.** V3 does, V4 refuses
  with `ENAMETOOLONG`, and `statvfs` reported neither, so eight tests checked
  the wrong branch. Two lines closed all eight.
- **A kernel panic on out of memory.** `test64` exists to check that the
  system responds sanely when memory runs out; it answers
  `kernel panic: pagefault in VM` and reboots. Open.
- **`mfs` dereferences NULL** in `lmfs_bflush()` from `lmfs_journal_commit()`,
  found by a cache test — a defect in the journal of milestone 7.4.

One number is about the tooling rather than the system: in the first run nine
hung tests took the whole machine down with them, and after the `longjmp` fix
the same tests hang while the system stays up and answers. They were being
killed by the `^C` the harness sent to interrupt them. The instrument was
damaging what it measured, and that was only visible once the damage was
fixed.

## Open questions, honestly labelled

These are known unknowns, not tasks with hidden answers.

- ~~**SMP occupancy tops out below two cores of four.**~~ Closed 2026-09-09:
  there was no ceiling, the benchmark was measuring message passing. Under the
  shell arithmetic loop it was measured with, one job spends 5.4% of its time
  in user mode and 28.6% in the kernel and in system processes - and system
  processes stay on the boot core by design, so such a load cannot use more
  cores however many jobs are added. With a load that really computes the same
  machine reports 74.7% user and 24.6% idle at eight jobs, which on four cores
  is the three the policy predicts. The meter had three faults, all pushing
  the same way; `kern.cp_time`, which answers this in one line, had been
  wired up from the kernel to `sysctl` all along and never used.
- ~~**`trace(1)` does not work on the machine.**~~ Closed 2026-09-09, and the
  cause was not in the code at all. The LP64 bug in the `ptrace()` wrapper was
  real and was fixed, but `trace`'s `mem.o` had been compiled before that fix
  and carried the truncation to 32 bits already generated. Relinking does not
  help when a *prototype* changes: the caller has to be recompiled.
- **Entropy.** The board has a hardware RNG and uses it. Its raw output is
  measurably biased — sixteen sigmas on the monobit test over 256 KB, which is
  ordinary for a ring oscillator and exactly why such sources are conditioned —
  while the pool's output is indistinguishable from the reference. These
  measures can only reject: a passed chi-square proves nothing about
  cryptographic strength and does not replace SP 800-90B. On QEMU there is no source at all, so the
  TCP initial-sequence secret is set *if possible* rather than required —
  waiting for entropy at boot is a reliable way to get a system that sometimes
  does not boot.

## What comes next

Stage 9 was the last stage the plan had, and 10 is not written yet. What is
open and named, in no particular order: 9.4 — the SD card controller, dynamic
linking (`ld.elf_so` links; `exec` has no `PT_INTERP` path), the bounce buffer
that still cuts every request into 32 KiB pieces, and two defects in
`servers/sched/schedule.c` found while closing the occupancy question —
`pick_cpu()` overwrites the scheduler's idea of a process's core on a quantum
expiry that never moves it, so `do_stop_scheduling()` later decrements the
wrong counter, and `cpu_is_available()` compares `CPU_DEAD` against an
unsigned counter.

## Deferred by decision

| Question | When |
|---|---|
| POSIX-compatible NetBSD userland, or a minimal one of our own | after stage 5 — still open |
| IPC model: as it is, a fastpath, or capabilities | **settled 2026-09-09: as it is** — see below |
| Dynamic linking | `ld.elf_so` links; `exec` needs a `PT_INTERP` path |

**The IPC model stays as it is, and that is a measurement rather than an
opinion.** The kernel can now count what it does — `KTRACE` in
`kernel/debug.h`, read through `/proc/ktrace`: every way into the kernel,
every IPC primitive, every kernel call by number, who caused each crossing,
and beside each way in the cycles spent in the kernel before leaving through
it. Run under QEMU's `-icount shift=0`, where the counter the kernel bills in
advances one tick per guest instruction, those cycles become instruction
counts.

One crossing of the kernel boundary costs 750–920 instructions, and it barely
matters what kind of crossing it is: a kernel call that reschedules nobody and
copies no message between processes costs 754, an IPC with a context switch
921. Of that, the architectural entry and exit is about 93 instructions — a
tenth — and copying the 128-byte message about 77. The rest is the generic
bookkeeping every crossing pays. So a fastpath that makes the *entry* cheaper
addresses a tenth of the cost; the earlier note here, which said the
measurements pointed at entry, was half right and is corrected. Capabilities
are a question about the rights model, not about cost, and no measurement can
settle them either way.

Three places where the cost actually is, each with a number: lazy FP switching
(12.7 % of kernel cycles, 0.74 traps per context switch, on workloads with no
floating point in them at all); the generic per-crossing bookkeeping; and the
userland — the shell's counting loop pays 13.7 kernel crossings per turn
because ash asks `tcgetattr` twice a turn. And three that turned out to cost
nothing: TLB flushes (138 per 112431 address-space switches — the ASID scheme
removed that item outright), quantum-expiry messages to the scheduler (seven
in eight seconds under full load), and message copying (8 %).

**Rust, so as not to return to it:** the interesting parts of a kernel end up in
`unsafe` anyway, and a mixed C/Rust kernel is the worst of both. Either all of
one or all of the other. With a fork as the strategy, it stays C.

**Formal verification is not a goal**, and seL4 is not a reference point. That
was decided at the start and is not being revisited.

## Ceilings that will have to move eventually

- **4 GB of physical memory**, from the free-page bitmap in `servers/vm/alloc.c`
  (`0x100000000/PAGE_SIZE` entries) and `VM_MAX_PHYS_MEM` beside it. The kernel
  does not need the limit.
- **4 GiB per process address space** (`USR_DATATOP`). This is VM's decision,
  not the architecture's: above 4 GiB `pt_pt[]` stops being a flat array, so
  what changes is the shape of `pagetable.c`.
- **MINIX upstream has not moved since 2018.** Nothing comes from there any
  more; what does come from outside comes from NetBSD-current, which has both a
  fresher upstream and the same build system.
