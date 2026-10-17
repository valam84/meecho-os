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
adreno_smmu.c@s/^\tad_wr(\&ad.smmu, SMMU_S2CR(i), S2CR_ROUTE_CB0);//@the stream routed before its S2CR says where
adreno_smmu.c@s/irq = ad.dt.smmu_irq\[ad.dt.smmu_nglobal + 0\];/irq = ad.dt.smmu_irq[0];/@a global line taken for bank 0's
adreno_smmu.c@s/^\tCB_WR(SMMU_CB_FSR, fsr);.*$//@the fault never cleared
adreno_smmu.c@s/^\t\tCB_WR(SMMU_CB_SCTLR, u.sctlr);//@bank 0 left enabled
adreno_smmu.c@s/	ad_wr(\&ad.smmu, SMMU_S2CR(i), S2CR_ROUTE_CB0);/	ad_wr(\&ad.smmu, SMMU_S2CR(i), S2CR_ROUTE_CB0 | 1);/@the stream routed to a bank that is not set up
adreno_smmu.c@s/	if (u.cb_touched \&\& keep_bank) {/	if (0 \&\& keep_bank) {/@bank 0 made stage 2 under an S2CR that names it
adreno_smmu.c@s/	test_s2 = (uint32_t)S2CR_TYPE_FAULT << 16;/	test_s2 = (uint32_t)S2CR_TYPE_TRANS << 16;/@the S2CR test translates (the tablet stopped)
adreno_zap.c@s/^\t(void)vm_unmap_phys(SELF, reg, (size_t)ad.dt.zap_sz);$//@the zap region still mapped at auth_and_reset
adreno_zap.c@s/^\t\tdev_put(reg + off + m.ph\[i\].filesz, NULL,$/\t\tif (0) dev_put(reg + off + m.ph[i].filesz, NULL,/@a segment's tail not zeroed
adreno_zap.c@s/^\tif (m.relocatable) {$/\tif (0) {/@no mem_setup for a relocatable image
adreno_zap.c@s/^\tmemcpy(meta + m.ehdr_size, m.hash_in_file/\tif (0) memcpy(meta + m.ehdr_size, m.hash_in_file/@the metadata without the hash segment
adreno_zap.c@s/if (a.res\[0\] == QCOMSCM_PAS_AUTHED)$/if (0)/@an authenticated image loaded again, not resumed
adreno_zap.c@s/if (a.res\[0\] == QCOMSCM_PAS_GIVEN) {/if (0) {/@TrustZone's region touched after a failed load
adreno_zap.c@s/off = m.relocatable ? m.ph\[i\].paddr - m.min_addr :/off = 0 ? 0 :/@a relocatable segment placed by its paddr
adreno_zap.c@s/^\t\tdev_put(reg + off, seg\[i\], m.ph\[i\].filesz);/\t\tdev_put(reg + off, seg[i], m.ph[i].filesz \& ~3u);/@a segment's unaligned end not copied
adreno_mdt.c@s/end = ((uint64_t)p->paddr + p->memsz + 0xfff) \& ~0xfffULL;/end = (uint64_t)p->paddr + p->memsz;/@the image's end not rounded to 4 KiB
adreno_mdt.c@s/m->split = 1;/(void)0;/@a split image read from the .mdt
adreno_cp.c@s/^\t(void)sys_cachectl(CACHE_CLEAN, cp.ring.cpu, cp.ring.size);$//@the ring not cleaned before the write pointer
adreno_cp.c@s/^\treturn sys_cachectl(CACHE_CLEAN, b->cpu, b->size);$/\treturn OK;/@the microcode not cleaned to memory
adreno_cp.c@s/return (0x9669u >> (v \& 0xf)) \& 1;/return 0;/@PM4 headers without their parity
adreno_cp.c@s/^\tif ((r = zap_load()) != OK)$/\tif ((r = OK) != OK)/@out of secure mode without the zap shader
adreno_cp.c@s/^\tW(A5XX_VBIF_XIN_HALT_CTRL0, 0xf);$//@GX collapsed with the VBIF's ports not halted
adreno_cp.c@s/^\tW(A5XX_RBBM_INT_0_MASK, 0);$//@the GPU's interrupts left unmasked
adreno_cp.c@s/^\tif ((r = smmu_attach()) != OK)$/\tif ((r = OK) != OK)/@the CP started with its stream routed nowhere
adreno_cp.c@s/^\t(void)sys_cachectl(CACHE_INVALIDATE, cp.mem.cpu, 4096);$//@the fence read from the cache, not memory
adreno_cp.c@s/out(0x0000002f);/out(0x0000002e);/@CP_ME_INIT not as Linux's
adreno_cp.c@s/{ 0x0080, 0x07444044 },/{ 0x0080, 0x07444004 },/@a clock-gating value mistyped
adreno_cp.c@s/((uint32_t)__builtin_ctz(len) << 24)/((uint32_t)(__builtin_ctz(len) + 1) << 24)/@CP_PROTECT lengths one power too long
adreno_cp.c@s/rmw(A5XX_RB_DBG_ECO_CNTL, 0, 1u << 9);/w(A5XX_RB_DBG_ECO_CNTL, 1u << 9);/@a read-modify-write that loses the other bits
adreno_cp.c@s/^\tW(A5XX_CP_RB_CNTL, A5XX_CP_RB_CNTL_VALUE);$//@the CP started without its ring's size
adreno_run.c@s/^\tcp_stop();.*$//@the CP never stopped before the power goes
EOF
echo "$caught of $n caught"
exit $rc
