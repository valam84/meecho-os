# How MEECHO is put together

This describes what is specific to this port. For the microkernel design
itself — servers, message passing, the reincarnation server — the MINIX 3
literature still applies unchanged; nothing about it was altered.

## The shape of the system

The kernel does four things: it translates memory, it delivers exceptions and
interrupts, it schedules, and it passes messages. Everything else is a process:

```
        user programs
  ───────────────────────────────────────────────────────────
   VFS   MFS   PFS   PM   VM   RS   DS   SCHED   MIB   procfs   lwip
   tty   sdmmc   dwmac   memory   random   virtio_blk   virtio_net
   xhci   usb_hub   usb_storage
  ───────────────────────────────────────────────────────────
                          kernel
```

A driver that faults takes down a driver. RS restarts it. That property is why
this system is worth porting rather than replacing.

Twelve of these are boot images: they travel with the kernel in a boot archive
and are started before there is a file system to load anything from.

## The address space

Granule 4 KB, 48 bits of address, four levels of translation.

```
0xffff_0000_0000_0000  ─┬─  KERNEL_VA_OFFSET
                        │   all of RAM, mapped linearly
                        │   the kernel, its data, device registers
0x0000_ffff_ffff_ffff  ─┴─
                            (a hole: bits 48..63 must be all 0 or all 1)
0x0000_0000_0000_0000  ───  the process, TTBR0, 4 GiB of it
```

**All of RAM is mapped linearly in the upper half**, so `phys2vir(pa)` is
`pa + KERNEL_VA_OFFSET` and any physical page is one pointer away. Three
consequences worth knowing before reading the code:

- `memory_init()` is empty, and the temporary-window machinery the 32-bit ports
  need (`freepdes`) does not exist here and will not.
- The linear map covers **RAM only**. Before dereferencing a physical address
  that came out of somebody else's page table, ask `pg_is_ram()`.
- The linear map carries kernel permissions, so a write to a copy-on-write page
  through it would succeed silently. `resolve()` in `memory.c` therefore reads
  the descriptor and checks `AARCH64_VM_AP_RO` itself, calling `vm_suspend()`
  where i386 would have taken a page fault. Faults do not need to be caught in
  those paths.

The kernel is linked at its virtual address and loaded at a physical one; the
ELF entry point is physical. Early code must be PC-relative, because the
address of a symbol is physical before the MMU comes up and virtual after. A
physical pointer written down before the switch does not work after it — that
cost two failures, `kern_phys_map` and `kinfo.kmessages`, and the helpers that
convert are idempotent so that code need not know which side of the switch it
is on.

`pre_init()` does not return. It builds the tables, enables the MMU, moves to
the upper half and calls `kmain()` from there. The 32-bit ports return into
`head.S`; they can, because one table covers both halves for them.

**Address spaces are ASID-tagged** and a context switch does not flush the TLB.
The tag is the process slot number plus one: there is no allocator and nothing
to free, because a slot holds one address space at a time, so live tags differ
by construction. The scheme is only used when the widest tag (`NR_PROCS`) fits
the hardware's ASID field — 16 bits on Cortex-A55 and A72, and on a machine
with 8 the old full flush stays. Two address spaces sharing a tag is not a slow
kernel, it is a wrong one.

## Exceptions

`VBAR_EL1` is set twice: once before the MMU, with the physical address of the
vector table, and again after the move, with the virtual one.

The frame for an exception from EL0 is built **directly in `p->p_reg`**. No
spare register is needed for it: `restore_user_context()` leaves `SP_EL1` at
the end of `p_reg`, and `p_reg` is the first field of `struct proc`, so
`sub sp, sp, #FRAME_SIZE` in the vector lands exactly there. Nothing is copied
on either transition. An exception from EL1 is nested and builds its frame on
the kernel stack. The offsets in `vectors.S` are checked against the structure
by a static assertion in `trap.c`.

## SMP

Secondary cores are started through PSCI, whose presence and calling
instruction (`hvc` or `smc`) come from the device tree. Four things about this
port's SMP are load-bearing:

- **`cpuid` is `TPIDR_EL1`.** The i386 trick — a number at the top of the
  kernel stack — cannot work here, because the stack an exception from EL0
  arrives on *is* the interrupted process.
- **The big kernel lock is taken and released in `context_stop()`**, not in
  assembly. Every path into the kernel must call it exactly once, and every
  path out must go through `switch_to_user()`.
- **An interrupt taken at EL1 is always the wake-up of an idle core** — only
  `halt_cpu()` unmasks interrupts in the kernel — and it has its own vector,
  `exception_entry_idle`, which does not return. Returning would mean returning
  with the lock held.
- **Each core has its own `TCR`.** `pg_load()` enables TTBR0 walks once, on the
  boot core; a secondary must call `pg_enable_user_walks()` for itself or it
  faults on the first user address and the kernel calls that "wrong user
  pointer".

## Interrupts: GICv2 and GICv3

The interrupt controller is not in the BSP, because every AArch64 machine has
one and only its version and addresses are properties of the machine — the same
QEMU `virt` gives either, depending on `gic-version=`. `arch/aarch64/gic.c`
reads the device tree and dispatches to `gicv2.c` or `gicv3.c`.

Three things in GICv3 fail silently if forgotten: `ICC_SRE_EL2.Enable` in
`head.S`, without which EL1 does not see the `ICC_*` registers at all, and only
on the path that enters at EL2; SGIs and PPIs live in the **redistributor**, not
the distributor, so that is where the timer is enabled; and `GICD_IROUTER`
exists only after affinity routing is turned on in `GICD_CTLR`.

The target board's device tree declares no CPU interface region at all, so
there was never a legacy mode to fall back to. This is also why the controller
was written before SMP: the IPI path goes through `GICD_SGIR` on GICv2 and
`ICC_SGI1R_EL1` on GICv3.

## FP/SIMD

Switched lazily, one owner per CPU; everyone else runs with a trap, and the
first FP instruction lands in `copr_not_available_handler()`. The state — 32
registers of 128 bits plus FPSR and FPCR — is saved by `fpu_asm.S`, in assembly
of necessity: the kernel's C is compiled `-mgeneral-regs-only`, and that flag
would narrow `.arch` for inline assembly too.

The scheme rests on **the kernel never touching FP registers**, since nothing
saves them on the way into an exception. The kernel has no floating point of
its own, but borrowed code did — GCC copies structures through `q29..q31` — so
`libminc`, `libsys`, `libexec` and `libtimers` are built `-mgeneral-regs-only`
on this architecture. To check:

```bash
aarch64-elf64-minix-objdump -d ~/obj-evbarm64/minix/kernel/kernel |
    grep -cE "[[:space:]][qv][0-9]+"
```

Only `fpu_save_regs` and `fpu_restore_regs` should match.

## The message ABI

The IPC message was 64 bytes with a 56-byte payload. Under LP64 that does not
hold the variants any more. It is now **120 bytes of payload, 128 total, 64-byte
alignment** — all 256 variants fit without re-laying out a single field, with
40 bytes to spare, and the message is exactly two cache lines on an A72.

The 256 `padding[]` arrays in `ipc.h` are **generated**; editing them by hand is
a mistake. After changing any field of any variant:

```bash
python3 minix/kernel/arch/aarch64/tools/gen-msgpadding.py
```

The generator takes the size from `M_PAYLOAD_SIZE`, verifies that no variant
lost a field, and compiles the result under both ABIs before writing it.

Measurement, not intuition, set this: entering the kernel costs 93 instructions
and copying 64 bytes costs 58, so a fastpath is a way to make the *entry*
cheaper and not a way to save on message size.

## No board tables

Every driver finds its hardware in the device tree, and the permission to touch
a register range comes from RS, granted by a line in `system.conf`:

```
service sdmmc { devicetree "rockchip,rk3568-dwcmshc"; ... }
```

That is the job the PCI server does on x86, and one entry serves both machines:
on aarch64 `MKPCI=no` so the PCI line is never read, and on i386 there is no
device tree so `devicetree` grants nothing.

The kernel hands the whole tree to user space through `sys_getinfo(GET_DTB)`;
`fdt_fetch()` in libsys brings it into a process, and the reader in
`<minix/fdt.h>` is shared by the kernel, RS and the drivers. `fdt_walk()`
carries the `#address-cells`/`#size-cells` stack itself, `fdt_node_reg()` reads
`reg` with the parent's widths, `fdt_node_gic_irq()` turns a triplet into a
line number, and `fdt_phandle_reg()` follows a phandle — which is how a
controller points at its GRF, its CRU and the one GPIO bank out of five that
belongs to it.

The DTB is never copied: its range is cut out of free memory, its physical
address is in `boot_dtb`, and after the move it is read through
`phys2vir(boot_dtb)`.

**A driver that cannot find its node says so and exits**, and the system boots
on. That is the agreement, and it is what makes the same image work on QEMU and
on the board.

## Device registers, and who maps them

The kernel maps its own device registers, and VM does not know about them.
`pre_init()` maps those ranges into TTBR1 before the MMU comes up, and
`pre_init_high()` moves the drivers onto the new addresses. `arch_phys_map()`
offers VM **only** the pages the kernel publishes into user space. Offering it
the device ranges would put the console at an address in a process's half of
the world, where it exists only while that process's tables are loaded.

This differs from the 32-bit ARM port, where the kernel and the process share a
table and the kernel's own mapping of the console *is* a mapping VM made.

## Cache maintenance for DMA

`sys_cachectl` — `CACHE_CLEAN`, `CACHE_INVALIDATE`, `CACHE_CLEAN_INVALIDATE`
over a range (`<minix/cachectl.h>`). It is a call of its own rather than a
`sys_vmctl` sub-operation, because permissions are granted per call and
`VMCTL_SETADDRSPACE` replaces any process's page table root — that would be a
poor trade for a network driver.

The range is **virtual, in the caller's own space**, and the translation is the
permission check: `resolve()` refuses an unmapped address and will not hand back
a writable pointer to a page the process may only read. A physical range would
be the right to throw away dirty lines of anybody's memory.

Two rules in it do not follow from first principles and were taken from
NetBSD's `_bus_dmamap_sync_segment()`: on a clean, a partially covered line is
**written back first**, because it also holds somebody else's bytes and they may
be dirty; and after a read from a device, invalidate **again**, because a
speculative fetch may have pulled the line back.

i386 answers `OK` and does nothing — DMA is coherent there, and that is an
answer rather than a stub. earm answers `ENOSYS`.

## Storage: MFS V4

The on-disk format is this port's own, and the reasoning is in the porting log.
Against V3: capability flags (`compat`/`ro_compat`/`incompat`) instead of eight
mandatory bits, a 128-byte inode with 64-bit size, nanosecond timestamps, 12
direct blocks and three levels of indirection, variable-length directory entries
carrying the file type and names up to 255 bytes, fast symbolic links in the
inode, little-endian and nothing else.

Both layouts live in one header, `minix/fs/mfs/ondisk.h`, read by the server,
`mkfs.mfs` and `fsck`. The in-core superblock and inode are separate structures
— which worked as long as there was only one format.

The block map does not know about levels: `map_slot()` answers which level a
block is on and `map_subtree()` descends. V4's third level is not a third
special case.

**The journal is in `libminixfs`**, not in the file system server, because the
cache is the one place a block reaches the disk from. Ordered mode: metadata
goes to the journal whole, data is written in place before the commit. The
order — data, journal, commit block, journal superblock, blocks home, journal
empty, with `bdev_flush()` between steps — is the entire guarantee.

Blocks of an open transaction are pinned the way any cache reader pins them,
and three things follow for free: they cannot be evicted, `lmfs_flushdev()`
cannot write them home, and the cache cannot be resized under an open
transaction.

The journal is a file on a reserved inode, contiguous and unchanged after
`mkfs` — which is the only reason its inode can be read *before* replay.

## Reading further

- [`port/PORTING-LOG.md`](../../port/PORTING-LOG.md) — why each of the above is
  the way it is, in the order it was discovered (Russian).
- [`PLAN.md`](../../PLAN.md) — the roadmap, milestone by milestone (Russian).
- [`CLAUDE.md`](../../CLAUDE.md) — a dense summary of the current state,
  maintained as working context (Russian).
- [ROADMAP.md](ROADMAP.md) — what is next, in English.
