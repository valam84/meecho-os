#!/bin/sh
# On the Mi Pad 4 under MEECHO: keep every core busy for SECONDS with work
# whose result is known, and count what came out wrong (port/clover/
# hw-cpu.md, step 3).  A core whose rail is too low for its clock does not
# always stop; it may get one result wrong in a million.  So: gzip and
# gunzip of the same random bytes, compared with their digest, and cpuhz's
# chain of adds, whose count must come out exact.
#
#	cpu-stress.sh SECONDS [WORKERS] [CPUHZ]
#
# WORKERS defaults to 8, one a core; CPUHZ to /root/cpuprobe/cpuhz.
S=${1:?seconds}
N=${2:-8}
HZ=${3:-/root/cpuprobe/cpuhz}
T=/tmp/cpu-stress
rm -rf $T
mkdir -p $T || exit 1
dd if=/dev/urandom of=$T/rnd bs=64k count=32 2> /dev/null || exit 1
want=$(md5 < $T/rnd)

worker() {
	end=$(( $(date +%s) + S ))
	ok=0 bad=0 hzok=0 hzbad=0
	while [ "$(date +%s)" -lt "$end" ]; do
		got=$(gzip -c $T/rnd | gunzip | md5)
		if [ "$got" = "$want" ]; then
			ok=$((ok + 1))
		else
			bad=$((bad + 1))
		fi
		# cpuhz: six runs, each "adds 200000000" when the adds are right.
		r=$($HZ | grep -c "adds 200000000 ")
		hzok=$((hzok + r))
		hzbad=$((hzbad + 6 - r))
	done
	echo "worker $1: gzip ok $ok bad $bad; cpuhz runs ok $hzok bad $hzbad"
}

i=1
while [ $i -le $N ]; do
	worker $i > $T/w.$i 2>&1 &
	i=$((i + 1))
done
sleep 5
ps -ax -o pid,cpuid,command 2> /dev/null | grep -e cpuid -e gzip -e cpuhz | head -20
wait
cat $T/w.*
bad=$(cat $T/w.* | awk '{ b += $7 + $13 } END { print b + 0 }')
echo "stress: $S s, $N workers, wrong results: $bad"
[ "$bad" -eq 0 ]
