#!/bin/bash
# A live update of a running service through RS, on the disk root (QEMU).
# It takes the kernel through do_update(), whose 5 KB frame overflowed the
# kernel stack until 2026-09-29 (KERNEL-AUDIT #9). What is expected now: the
# system stays up. The update itself still fails in RS's state transfer
# (ENOSYS) and is rolled back - a separate matter.
# Run in WSL: bash port/test/live-update.sh
{ sleep 50; echo root; sleep 3;
  echo 'ls -l /service/random'
  sleep 2
  echo 'minix-service update /service/random -label random; echo "rc=$?"'
  sleep 15; echo 'echo alive'; sleep 3; } |
  QEMU_SMP=${SMP:-1} QEMU_TIMEOUT=90 bash /home/minix/bin/ramimage.sh -d 2>&1 |
  sed -n '\|ls -l /service/random|,$p' | head -40
