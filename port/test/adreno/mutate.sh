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
# The render node (msmtest.c): adreno_gpu.c, adreno_drm.c, the serving SMMU and CP.
adreno_gpu.c@s/^\t\t\tinval_due = 1;$/\t\t\t;/@no UCHE invalidation at a switch of tables
adreno_gpu.c@s/^\t\t\tif (!TAILQ_EMPTY(\&flight))$/\t\t\tif (0)/;s/if (!quiesce(100)) {/if (0) {/@a client's table loaded under another's running jobs
adreno_smmu.c@s/return tlbi();\t\t\/\* nothing of the last table remembered \*\//return OK;/@a table switch the TLB does not hear of
adreno_gpu.c@s/(int32_t)(fence - j->seqno) >= 0/(int32_t)(fence - j->seqno) > 0/@a job retired one fence late
adreno_gpu.c@s/^\t\tdrmdrv_mem_for_cpu(j->bos\[i\]->mem);$//@the GPU's writes never shown to the processor
adreno_gpu.c@s/^\t\t\tdrmdrv_mem_for_gpu(j->bos\[i\]->mem);$//@what the client wrote not cleaned before its job
adreno_gpu.c@s/^\t\tj->fault_fsr = fsr;$/\t\tj->fault_fsr = fsr; j->result = OK;/@a fault in a job forgotten
adreno_gpu.c@s/^\tsmmu_line();$//@the fence's line taken before the fault's - the job ends well
adreno_gpu.c@0,/^\tcp_emit(0);$/s//\tcp_emit(1);/@the save address written in protected mode
adreno_gpu.c@s/cp_emit(CP_EVENT_WRITE_IRQ | CACHE_FLUSH_TS);/cp_emit(CACHE_FLUSH_TS);/@a job's fence without the GPU's line
adreno_gpu.c@s/^\t\trecover("gpu: a job made no progress");$/\t\t(void)0;/@a hang never recovered
adreno_gpu.c@s/^\tif (cp_halt() != OK)$/\tif (0)/@a hang's recovery powers the GPU off under a running CP
adreno_gpu.c@s/^\t\t\tj->result = EIO;$/\t\t\t(void)0;/@the jobs in the ring at a hang end well
adreno_gpu.c@s/^\t(void)smmu_load(NULL);$/\t;/@a client's table freed while bank 0 walks it
adreno_gpu.c@s/if (++hang_looks < 2) {/if (0) {/@a hang called at the first look
adreno_gpu.c@s/^\t(void)cp_irq();$//@the GPU's line left masked after the first command
adreno_drm.c@s/((flags \& MSM_BO_GPU_READONLY) ? 0 :/(0 ? 0 :/@a read-only buffer mapped writable
adreno_drm.c@s/^\t(void)smmu_pt_unmap(\&bo->vm->pt, bo->iova, bo->size \/ PAGE);$//@a buffer freed still in its table
adreno_smmu.c@s/return pt == loaded \&\& cc_smmu_clocked(\&why) ? tlbi() : OK;/return (void)\&why, OK;/@a buffer out of the loaded table, still in the TLB
adreno_drm.c@s/^\t\t\tdrmdrv_job_uses(\&j->dj, j->bos\[i\]->mem);$/\t\t\t(void)drmdrv_job_touches(\&j->dj, j->bos[i]->mem);/@no implicit order on shared buffers
adreno_drm.c@s/deadline_of(\&w->timeout), ETIMEDOUT);/deadline_of(\&w->timeout), EBUSY);/@WAIT_FENCE past its deadline: EBUSY, not ETIMEDOUT
adreno_drm.c@s/^\t\ta->q->faults++;$/\t\t;/@a queue's faults not counted
adreno_drm.c@s/((c >> 8) \& 0xff)/((c >> 4) \& 0xff)/@GPU_ID misread from the chip id
adreno_drm.c@s/^\tq->fences\[q->nfences\].seq = seq;$/\treturn OK;/@a queue's fences not kept for WAIT_FENCE
adreno_drm.c@s/s->fence = ++q->seq;/s->fence = q->seq++;/@fence numbers one behind
adreno_drm.c@s/if (g->len >= sizeof(bo->name))/if (g->len > sizeof(bo->name))/@a name with no room for its zero taken
adreno_drm.c@s/^\t\t\t\tgoto fail;\t\/\* twice in one list \*\/$/\t\t\t\t(void)0;/@a buffer twice in one submission
adreno_drm.c@s/j->bos\[sc.submit_idx\]->size || sc.size \/ 4 >= (1u << 20))/~0ull || sc.size \/ 4 >= (1u << 20))/@a command buffer past its buffer
adreno_drm.c@s/^\tgpu_vm_gone(\&a->vm);$//@a client's table freed while bank 0 may walk it (no switch)
adreno_drm.c@s/(p->op \& MSM_PREP_NOSYNC) ?/0 ?/@CPU_PREP with NOSYNC waits
adreno_drm.c@s/s->flags != 0 || s->prio >= NR_PRIORITIES/s->flags != 0 || s->prio > NR_PRIORITIES/@a queue of priority 3 taken
adreno_drm.c@s/if (p->pipe != MSM_PIPE_3D0 || p->pad != 0 || p->len != 0)/if (p->pad != 0 || p->len != 0)/@GET_PARAM of the 2D pipe answered
adreno_cp.c@s/^\t\t(void)sys_irqenable(\&cp.hook);$/\t\t(void)0;/@the GPU's line never enabled again
adreno_cp.c@s/^\tW(A5XX_RBBM_INT_CLEAR_CMD, st \& ~A5XX_INT_AHB_ERROR);$//@the GPU's interrupt status never cleared
adreno_smmu.c@s/for (i = 0; i < nglobal; i++)/for (i = 0; i < nglobal \&\& 0; i++)/@a client's table without the service's buffers
adreno_smmu.c@s/^\tCB_WR(SMMU_CB_FSR, \*fsr);$//@a fault in bank 0 never cleared
adreno_cp.c@s/return mem_word(MEM_SHADOW) == cp.wptr \&\&/return G(A5XX_CP_RB_RPTR) == cp.wptr \&\&/@the read pointer from the CP's register: the GPU never looks idle (the tablet's first round)
# The core's clock (t_clock): adreno_cc.c's PLLs, adreno_gpu.c's choice.
adreno_cc.c@s/^\tp = cur == 0 ? 1 : 0;$/\tp = 0;/@(clock) no ping-pong: the PLL the core runs from programmed again
adreno_cc.c@s/PLL_MODE_LOCK_DET, PLL_MODE_LOCK_DET,$/0, 0,/@(clock) the root moved before the PLL locked
adreno_cc.c@/the root did not take /,/pll_stop((unsigned)cur)/s/pll_stop((unsigned)cur)/pll_stop(p)/@(clock) the new PLL stopped, not the one left
adreno_cc.c@s/{ 160, 640, 7 }/{ 160, 640, 3 }/@(clock) 160 MHz's divider wrong
adreno_cc.c@s/khz >= 500000 ? 2 : 3;/khz >= 500000 ? 3 : 2;/@(clock) the VCO band wrong
adreno_cc.c@s/^\t\tif (!pll_used\[i\])$/\t\tif (1)/@(clock) the PLLs not put back
adreno_cc.c@s/^\tmicro_delay(5);\t\t\/\* bypass off, then the reset released \*\/$//@(clock) the reset released with the bypass
adreno_cc.c@s/^\tad_wr(\&ad.gpucc, b + PLL_CONFIG_CTL, PLL_CONFIG_GPU);$//@(clock) the PLL's configuration not written
adreno_cc.c@s/PLL_USER_VCO(vco) | PLL_USER_ALPHA_EN);/PLL_USER_VCO(vco));/@(clock) the PLL's fraction not enabled
adreno_cc.c@s/user | PLL_USER_MAIN |$/user |/@(clock) the PLL's main output not enabled
adreno_gpu.c@s/if (level > mx_voted \&\& (r = mx_vote(level)) != OK \&\&/if (0 \&\& (r = mx_vote(level)) != OK \&\&/@(clock) the clock up without VDDMX voted up
adreno_gpu.c@s/^\tmx_release();$//@(clock) the VDDMX vote left when the GPU goes
adreno_gpu.c@s/if (!(ad.dt.opp\[i\].hw \& bin_hw))/if (0)/@(clock) the speed bin not looked at
adreno_gpu.c@s/{ 122, 3 }/{ 122, 4 }/@(clock) the bin table off by one
adreno_run.c@s/^\tv >>= ad.dt.bin_shift;$//@(clock) the fuse's bits not shifted
EOF
echo "$caught of $n caught"
exit $rc
