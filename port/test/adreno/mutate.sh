#!/bin/sh
# Мутации службы adreno: каждая — правдоподобная ошибка в копии её файлов.
# Стенд (run.sh) обязан её поймать: упасть с зависанием шины в модели, с
# состоянием «не как было» или без нужной строки Q.  Мутация, которую
# стенд не поймал, — дыра в стенде, а не в службе.  Мутация, которая не
# собралась, ничего не доказывает и считается отказом самого списка.
#
#	sh mutate.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
SRC=$TREE/minix/drivers/video/adreno
T=${TMPDIR:-/tmp}/adreno-mutate
rc=0 n=0 caught=0

# файл@выражение sed@что это за ошибка
while IFS='@' read -r file expr what; do
	[ -z "$file" ] && continue
	case "$file" in \#*) continue ;; esac
	n=$((n + 1))
	rm -rf "$T"; mkdir -p "$T"
	cp "$SRC"/*.c "$SRC"/*.h "$T"/
	sed -i "$expr" "$T/$file"
	if cmp -s "$SRC/$file" "$T/$file"; then
		echo "NOT APPLIED  $what"
		rc=1
		continue
	fi
	if SRC=$T OUT=$T sh "$HERE/run.sh" > "$T/out" 2>&1; then
		echo "MISSED       $what"
		rc=1
	elif grep -q 'error:' "$T/out"; then
		echo "NO BUILD     $what: $(grep -m1 'error:' "$T/out" | cut -c1-100)"
		rc=1
	elif ! grep -q 'FAIL' "$T/out"; then
		echo "CRASHED      $what: $(tail -1 "$T/out" | cut -c1-100)"
		rc=1
	else
		caught=$((caught + 1))
		echo "caught       $what: $(grep -m1 'FAIL' "$T/out" | sed 's/^ *FAIL: //' | cut -c1-90)"
	fi
done <<'EOF'
adreno_cc.c@s/if (!iface_on()) {/if (!iface_on() \&\& 0) {/@GPUCC read without checking the bus clock
adreno_cc.c@s/if (sdm660_gpucc_gdsc\[i\].parent >= 0 \&\&/if (0 \&\&/@GX up without its parent CX
adreno_cc.c@s/^\t\tgp_clear(sdm660_gpucc_gdsc\[i\].clamp, CLAMP_IO);//@GX's I/O clamp never released
adreno_cc.c@s/gp_wait(st, GDSC_PWR_ON, GDSC_PWR_ON, 2000)/gp_wait(st, 0, 0, 2000)/@PWR_ON not waited for
adreno_cc.c@s/ad_rd(\&ad.gcc, en) | (1u << bit)/ad_rd(\&ad.gcc, en) | (2u << bit)/@GCC branch enabled by the wrong bit
adreno_cc.c@0,/gp_set(sdm660_gpucc_rcg\[r\].cmd, RCG_CMD_UPDATE);/s///@root moved without UPDATE
adreno_cc.c@s/^\t\tgp_set(undo.rcg\[i\].cmd, RCG_CMD_UPDATE);//@root not put back
adreno_cc.c@s/^\t\tgp_set(sdm660_gpucc_gdsc\[g\].gdscr, GDSC_SW_COLLAPSE);//@domains left powered
adreno_cc.c@s/^\t\t\tgp_clear(sdm660_gpucc_branch\[b\].cbcr, CBCR_ENABLE);/\t\t\t(void)b;/@branches left running
adreno_cc.c@s/if (\*why == NULL \&\& !gdsc_chain_on(ad.dt.smmu_pd))/if (0 \&\& !gdsc_chain_on(ad.dt.smmu_pd))/@GPU guard ignores the domain
adreno_cc.c@s/if (\*why == NULL \&\& (ad.dt.smmu_pd < 0 ||/if (0 \&\& (ad.dt.smmu_pd < 0 ||/@SMMU guard ignores the domain
adreno_hw.c@s/^\tad_wr(\&ad.gpu, A5XX(A5XX_CP_SCRATCH_REG(0)), old);//@scratch not restored
adreno_find.c@s/clk\[n\].id = cells == 1 ?/clk[n].id = cells == 2 ?/@clock numbers misread
adreno_find.c@s/(rev % 10) << 8/(rev % 10) << 4/@chip id misassembled
adreno_run.c@s/if (r == OK \&\& stage >= AD_GPU)/if (stage >= AD_GPU)/@GPU read after a failed power-up (guard must hold)
EOF
echo "$caught of $n caught"
exit $rc
