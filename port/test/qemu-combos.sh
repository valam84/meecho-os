cd /home/minix
for c in "" "-2" "-3" "-2 -3"; do
  for smp in 1 4; do
    out=$( { sleep 20; echo root; sleep 4; echo "cat /proc/cpuinfo | head -3; cat /proc/uptime; sleep 2; cat /proc/uptime"; sleep 5; echo /sbin/poweroff; sleep 6; } | QEMU_SMP=$smp QEMU_TIMEOUT=60 bash /home/minix/bin/ramimage.sh -r $c 2>&1 )
    login=$(echo "$out" | grep -c "login:"); up=$(echo "$out" | grep -cE "^[0-9]+\.[0-9]+ "); panic=$(echo "$out" | grep -ci "panic")
    echo "combo [$c] smp=$smp: login=$login uptime_lines=$up panic=$panic"
    echo "$out" | grep -iE "panic|kernel exception|SError|CPU . is up|impossible" | head -5
  done
done
