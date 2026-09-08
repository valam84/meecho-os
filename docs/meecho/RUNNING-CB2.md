# Running MEECHO on a BIGTREETECH CB2

The CB2 is a Rockchip RK3566 board: four Cortex-A55 cores, 2 or 4 GB of LPDDR4,
on-board eMMC, gigabit Ethernet, and a serial console on `uart2` at
**1500000 8N1**. It is the target this port was written against;
[HARDWARE-CB2.md](HARDWARE-CB2.md) has the register-level detail.

Everything here is arranged so that **a board you cannot physically reach is
not a board you can lose.** MEECHO boots once, from a flag that U-Boot clears
before starting anything; the next power cycle returns the vendor Linux with
its ssh. And a kernel that boots but never reaches a prompt resets itself
after fifteen minutes.

## What you need

- a CB2 with the vendor image (Armbian/BTT) on its **SD card** — we use its
  U-Boot, not its system;
- a second, blank SD card is *not* needed: MEECHO installs beside the vendor
  system on the same card;
- a USB-TTL adapter on `uart2` for the console. Not strictly required once
  networking works, but the first boot of anything new is watched on the wire;
- optionally, ssh access to the vendor Linux — this is what makes installing a
  new build take a minute instead of a card swap.

> Building your own U-Boot is not worth it and is the one irreversible change
> on this board: RK3566 needs proprietary `rkbin` blobs, and the vendor's build
> is known to work on this hardware. We take control where the vendor's
> bootloader offers it — it reads `boot.scr` from the first partition.

## The quick path: boot MEECHO from the vendor card

From a release, unpack `meecho-cb2-0.1.0.tar.gz` and copy two things onto the
card's FAT partition (the one the board sees as `/boot`):

```
meecho/          the whole directory: kernel.bin and boot.mba
fixup.scr        alongside the vendor's boot.scr, which is left untouched
```

Then, on the running vendor Linux, arm the one-shot flag and reboot:

```bash
touch /boot/meecho.go
reboot
```

U-Boot removes `meecho.go` before it hands over, so this boots MEECHO exactly
once. Watch the console at 1500000 8N1:

```
MEECHO: loading from mmc 1:1
MEECHO: booting, archive 11505664 bytes
Starting kernel ...
MEECHO/aarch64: 4 cores, memory 0000000000200000-0000000080000000
...
login:
```

`root`, no password. `/sbin/reboot` returns you to Armbian.

Replacing `boot.scr` outright, rather than adding `fixup.scr`, makes MEECHO the
default; rename the vendor's file first so you can put it back. The boot script
source is [`port/cb2-boot.cmd`](../../port/cb2-boot.cmd) with the load
addresses and the reasoning for each; recompile it with
`mkimage -C none -A arm64 -T script -d boot.cmd boot.scr`.

## The root file system on eMMC

The card kit boots a ramdisk root, which is small and has no userland to speak
of. The real system lives on the board's eMMC:

```bash
# on the vendor Linux, with root-emmc.img.gz copied over
gunzip -c root-emmc.img.gz | dd of=/dev/mmcblk1 bs=4M conv=fsync
```

That overwrites the first gigabyte of the eMMC, which the vendor system does
not use — but read that sentence twice before running it.
[`port/board-root.sh`](../../port/board-root.sh) does the same over ssh.

Then boot with the root on eMMC by touching `/boot/meecho/root_emmc`, or pass
`-e` to `board-cycle.sh`. The boot arguments become
`rootdevname=c0d0 blkdrv=sdmmc`: there are two block drivers now, and `rc` does
not guess which one this machine has — a driver that cannot find its node in
the device tree fails loudly, and there is no cheap probe from a shell script.

The image is made from the same `proto` file that produces the QEMU disk
([`port/mkroot.sh`](../../port/mkroot.sh)), with one substitution: `/etc/rc`.
So the board's root and the emulator's root are built by the same code, and the
only difference between them is the medium.

## Building the kit yourself

```bash
bash port/build-all.sh              # kernel.bin and boot.mba
bash port/mkcard.sh                 # the card kit, into ~/obj-evbarm64/work/card
bash port/mkroot.sh                 # root-emmc.img.gz
```

`mkcard.sh` needs `mkimage` (`apt install u-boot-tools`).

`mkroot.sh` puts an `authorized_keys` into the image if it finds
`port/authorized_keys` — that file is not in the repository, because a key
belongs to a person and not to an operating system. Drop your public key there
before building if you want to ssh in.

## Boot-time switches

Files in `/boot/meecho/` on the card, read by the boot script:

| File | Effect |
|---|---|
| `root_emmc` | root on eMMC (`rootdevname=c0d0 blkdrv=sdmmc`) instead of the ramdisk |
| `no_smp` | boot one core only. Useful as the control in an SMP measurement |
| `no_bootwd` | do not arm the watchdog. For an interactive session longer than 15 minutes |
| `slow_console` | 115200 instead of 1500000 |
| `verbose` | `verbose=3`: the kernel narrates its own initialisation |

**`bootwd=900` is armed by default**: fifteen minutes after boot the kernel
resets the machine through PSCI, from the clock tick, which arrives even when
the system is starving. Since U-Boot has already cleared the one-shot flag,
that reset lands back in Armbian rather than in a loop. A run that never
reached a prompt no longer needs someone standing at the power switch.

**Use 115200 for any run you will have to quote.** At 1500000 through a cheap
adapter, characters are dropped silently, and what gets dropped is exactly the
word a script is waiting for. The full rate is fine for checking whether the
board is alive.

## A full automated run

[`port/board-cycle.sh`](../../port/board-cycle.sh) does one run end to end:
puts a schedule on the console machine, starts the console **before** rebooting
the board — otherwise the beginning of the boot, which is where everything
interesting is printed, does not make it into the log — arms the flag, reboots,
and hands back the log.

```bash
cp port/board.conf.example port/board.conf   # your board, your console machine
bash port/board-cycle.sh -e -s port/docs/run1.sched /tmp/run.log
```

Take the log **immediately**: the next run truncates it on the console machine.

Two machines are involved because the console and the system on the board fail
at different moments and for different reasons. They may be the same machine;
set both addresses the same if so.

## Working on a running MEECHO

Once the network is up, ssh replaces the console for asking questions — a
question that used to cost fifteen minutes of scripted run costs a second, and
the next question can depend on the answer.

```bash
ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@<board>
```

The host key options are not sloppiness: the board has one address for both of
its systems, MEECHO generates its host key on first boot, and one run is enough
to make ssh refuse to talk to the Armbian you install the next build through.

**`PATH` for a non-interactive ssh does not contain `/sbin`** — `/etc/profile`
adds it, and only a login shell reads that. `reboot` is not found and the
command silently does nothing; write `/sbin/reboot`.

**Installing a new build did not get cheaper.** `kernel.bin` and `boot.mba`
live on the card's FAT partition, and MEECHO has three file systems — `mfs`,
`pfs`, `procfs` — none of which is FAT; the eMMC root is written with `dd` from
the vendor Linux. So a change in the **kernel or the boot image** still costs a
full cycle through Armbian, while a change in **userland** costs a minute:
cross-build it and pipe it in with `ssh ... 'cat > /bin/thing'`. `scp` does not
work — `sftp-server` builds but is not enabled, it needs real `*at()` calls.

## Before a run that changes the system on the board

**Check that the console is alive and printing.** Once, early on, the serial
adapter disappeared from the host at the exact moment of the first MEECHO boot
and did not come back until it was moved to a different machine. The cause was
never established and it has not recurred in dozens of runs since, so the
question was retired with that setup rather than answered. The practice does
not depend on the cause: find out that the console works before the run, not
from its silence afterwards.
