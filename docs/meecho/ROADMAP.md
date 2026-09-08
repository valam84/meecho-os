# Roadmap

The full plan, with the reasoning for each decision, is [`PLAN.md`](../../PLAN.md)
(Russian). This is the summary and the state as of **2026-09-08**.

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

## Next: stage 9, storage at the speed of the hardware

The eMMC driver works and is **slow twice over**: data moves through a FIFO with
the CPU, and waiting is a poll with a deadline. Both were deliberate, and
neither is justified any more.

- The header of `sdmmc_sdhci.c` explains the absence of DMA by saying the port
  has no way to ask for cache maintenance from a driver. Since 2026-09-07 it
  has: `sys_cachectl`. **That comment is now wrong and must be rewritten with
  the code**, or it will lie to the next reader.
- The interrupt was not wired up "one unknown at a time". RS hands out the line
  from the device tree (eMMC is SPI, line 51), and `sys_irqsetpolicy` works in
  this port for both `dwmac` and `tty`.
- `host->max_blocks = 4` is not a choice but the controller's PIO buffer
  ceiling — four blocks pass, eight do not, and the flow control the standard
  promises is absent in this part. With DMA the ceiling goes away with its
  cause.

**9.1 — the number first.** Measure before the change and the same way after,
or "it got faster" is not a result. From the board over ssh: `dd if=/dev/c0d0
of=/dev/null bs=64k count=N`, the time to unpack a tar, the time from
`Starting sshd` to `login:`.

**9.2 — DMA.** SDMA or ADMA2, decided by the controller's capability register
rather than in advance. The buffer is handled by `sys_cachectl` under the rule
taken from NetBSD. Then drop `max_blocks` and check that a long transfer really
goes through. Corruption here is quieter than failure: compare contents, do not
just ask whether the transfer returned.

**9.3 — the interrupt.** `sys_irqsetpolicy` on line 51; the handler clears the
cause in Normal Interrupt Status and waiting becomes a message. The polling path
stays as a fallback — on this board a register has already failed to do what the
specification promised.

**9.4 — the SD card controller.** `mmc@fe2b0000` is a DesignWare mobile storage
host: different registers, its own clock-update command, no driver. A file
beside `sdmmc_sdhci.c` behind the same host table.

**Criterion:** eMMC reads and writes through DMA, on an interrupt, with no
four-block ceiling, and the difference from today is a measured number on the
board.

## Open questions, honestly labelled

These are known unknowns, not tasks with hidden answers.

- **SMP occupancy tops out below two cores of four.** Adding jobs does not raise
  it. Expected around three, since `pick_cpu()` gives the boot core to system
  processes and spreads user processes over the rest. A candidate explanation is
  named in the porting log (milestone 8.0.3) and **not one line of it is
  confirmed**.
- **`trace(1)` does not work on the machine** ("Kernel magic check failed"). A
  genuine LP64 bug was found and fixed on the way — the `ptrace()` wrapper in
  libc returned `int` where a machine word goes over the wire — but the symptom
  survived it, and `struct proc` has the same layout for `trace` and for the
  kernel. The cause is not established, and no story has been invented for it.
- **`fpu_sigcontext()` is unimplemented.** A signal handler sees the interrupted
  code's FP state and vice versa. It needs the layout of `struct sigcontext`
  agreed with libc, not three lines in the kernel.
- **Entropy.** The board has a hardware RNG and uses it; nothing is claimed
  about the quality of its numbers. On QEMU there is no source at all, so the
  TCP initial-sequence secret is set *if possible* rather than required —
  waiting for entropy at boot is a reliable way to get a system that sometimes
  does not boot.

## Deferred by decision

| Question | When |
|---|---|
| POSIX-compatible NetBSD userland, or a minimal one of our own | after stage 5 — still open |
| IPC model: as it is, a fastpath, or capabilities | after stage 4; the measurements say a fastpath would be about the cost of *entry* |
| Dynamic linking | `ld.elf_so` links; `exec` needs a `PT_INTERP` path |

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
