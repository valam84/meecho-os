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
adreno_smmu.c@0,/if (!cc_smmu_clocked(\&why)) {/s//if (0) {/@SMMU written without its clocks
adreno_smmu.c@s/ad_wr(\&ad.smmu, SMMU_SMR(i), 0x7fffu);/ad_wr(\&ad.smmu, SMMU_SMR(i), SMR_VALID | 0x7fffu);/@the SMR test routes a stream
adreno_pt.c@0,/return clean(\&t->e\[i\], sizeof(t->e\[i\]));/s//return OK;/@a page entry not cleaned to memory
adreno_pt.c@s/if (clean(\&t->e\[i\], sizeof(t->e\[i\])) != OK)/if (0)/@a table entry not cleaned to memory
adreno_pt.c@s/if (clean(t->e, PT_PAGE) != OK) {/if (0) {/@a new table not cleaned whole
adreno_pt.c@s/PTE_PAGE | PTE_AF | /PTE_PAGE | /@a page without the access flag
adreno_pt.c@s/pte |= PTE_AP_RDONLY;/(void)0;/@a read-only page left writable
adreno_pt.c@s/(39 - 9 \* level)/(40 - 9 * level)/@table indices misread
adreno_smmu.c@0,/if ((r = tlbi()) != OK)/s//if ((r = OK) != OK)/@the TLB not invalidated before the bank is enabled
adreno_smmu.c@s/if (pt_unmap(\&u.pt, VA_A) == OK \&\& tlbi() == OK)/if (pt_unmap(\&u.pt, VA_A) == OK)/@the TLB not invalidated after an unmap
adreno_smmu.c@s/TCR_T0SZ(64 - g.ias)/TCR_T0SZ(64 - 36)/@the input width told wrong
adreno_smmu.c@s/^\tad_wr64(\&ad.smmu, g.cb0 + SMMU_CB_TTBR0, ttbr0);//@the bank enabled without its tables
adreno_smmu.c@s/ad_wr64(\&ad.smmu, g.cb0 + op, va \& ~0xfffull);/ad_wr(\&ad.smmu, g.cb0 + op, (uint32_t)va);/@a translation operation written by halves
adreno_smmu.c@s/^\tad_wr(\&ad.smmu, SMMU_S2CR(i), ((uint32_t)S2CR_TYPE_TRANS << 16) | 0);//@the stream routed before its S2CR says where
adreno_smmu.c@s/irq = ad.dt.smmu_irq\[ad.dt.smmu_nglobal + 0\];/irq = ad.dt.smmu_irq[0];/@a global line taken for bank 0's
adreno_smmu.c@s/^\tCB_WR(SMMU_CB_FSR, fsr);.*$//@the fault never cleared
adreno_smmu.c@s/^\t\tCB_WR(SMMU_CB_SCTLR, u.sctlr);//@bank 0 left enabled
EOF
echo "$caught of $n caught"
exit $rc
