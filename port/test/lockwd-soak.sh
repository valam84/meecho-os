#!/bin/bash
# The lock watchdog (kernel/arch/aarch64/lockwd.c) must stay quiet under load.
# Four cores, root on disk, EL2 + GICv3: two counting loops and three loops
# of file-system work for a minute, then poweroff. Anything printed about the
# lock watchdog, a panic or a core that did not stop is a failure.
# Run in WSL: bash port/test/lockwd-soak.sh
{ sleep 50; echo root; sleep 3;
  echo 'for i in 1 2; do (while :; do :; done) & done'
  echo 'for i in 1 2 3; do (while :; do ls -lR / >/dev/null 2>&1; cp -r /etc /tmp/e$i; rm -r /tmp/e$i; done) & done'
  sleep 60; echo 'cat /proc/uptime'; sleep 3; echo /sbin/poweroff; sleep 40; } |
  QEMU_SMP=4 QEMU_TIMEOUT=190 bash /home/minix/bin/ramimage.sh -d -2 -3 2>&1 |
  grep -iE "lock watchdog|panic|did not stop|^[0-9]+\.[0-9]+|has halted"
