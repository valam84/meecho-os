#!/bin/bash
# Build the generic kernel; print the first error and the exit status.
cd /home/minix/minix-src || exit 1
LOG=/home/minix/kernel-build.log
/home/minix/tools-evbarm64/bin/nbmake-evbarm64-el -C minix/kernel -j24 dependall > $LOG 2>&1
rc=$?
grep -m5 -E "error|Error|warning" $LOG
echo "rc=$rc"
exit $rc
