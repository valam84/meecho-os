#!/bin/sh
# Проверка стенда: каждая мутация - одна правдоподобная ошибка в копии
# службы, и стенд обязан её поймать.  Стенд, который проходит на
# испорченной службе, ничего не доказывает о целой.
#
#	sh mutate.sh
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
ORIG=$TREE/minix/drivers/video/mali
WORK=${WORK:-/tmp/mali-mutate}

# имя@файл@что заменить@на что (для sed: & писать как \&, * как \*,
# [ как \[; разделитель - %)
MUTATIONS='
idle-bit@mali_rk.c@#define  PD_GPU_IDLE		(1u << 1)@#define  PD_GPU_IDLE		(1u << 2)
pwr-bit@mali_rk.c@#define  PD_GPU_PWR		(1u << 0)@#define  PD_GPU_PWR		(1u << 1)
pvtpll-bit@mali_rk.c@(((x) >> 11) \& 0x1)@(((x) >> 10) \& 0x1)
div-field@mali_rk.c@(GPU_SRC_GPLL << 6) | (div - 1)@(GPU_SRC_GPLL << 6) | div
safe-rate@mali_rk.c@#define GPU_CLK_TARGET_KHZ	200000@#define GPU_CLK_TARGET_KHZ	400000
fields-mask@mali_rk.c@#define CLKSEL_GPU_FIELDS	0xfbcfu@#define CLKSEL_GPU_FIELDS	0xffffu
aclk-ref@mali_rk.c@#define GPU_ACLK_DIV_REF	2@#define GPU_ACLK_DIV_REF	1
ref-domain@mali_rk.c@{ "PWR_STATUS",   1, PMU_PWR_STATUS, 0x0000007e, PD_GPU_PWR },@{ "PWR_STATUS",   1, PMU_PWR_STATUS, 0x0000007f, PD_GPU_PWR },
gate-mask@mali_rk.c@want << 16);@want);
pll-postdiv@mali_rk.c@pd2 = (con1 >> 6) \& 0x7;@pd2 = (con1 >> 6) \& 0x3;
irq-order@mali_find.c@"gpu", "job", "mmu"@"gpu", "mmu", "job"
clock-cells@mali_find.c@p + idx \* 8 + 4@p + idx \* 8
supply-walk@mali_find.c@(void)fdt_walk(dtb, find_supply, info);@;
leaf-type@mali_mmu.c@prot | MALI_PTE_SH_OUTER | MALI_PTE_LEAF;@prot | MALI_PTE_SH_OUTER | MALI_PTE_TABLE;
leaf-write@mali_bo.c@prot = MALI_PTE_READ | MALI_PTE_WRITE;@prot = MALI_PTE_READ;
level-shift@mali_mmu.c@(va >> (39 - 9 \* level))@(va >> (38 - 9 * level))
read-inner@mali_mmu.c@AS_TRANSTAB_ADRMODE_TABLE | AS_TRANSTAB_READ_INNER;@AS_TRANSTAB_ADRMODE_TABLE;
memattr@malireg.h@0x00888d88u@0x008d8888u
wv-value@malireg.h@JOB_WV_VALUE[[:space:]]*48@JOB_WV_VALUE 40
job-index@mali_job.c@JOB_CTL_INDEX(i + 1)@JOB_CTL_INDEX(i)
next-ptr@mali_job.c@JOB_VA + (i + 1) \* JOB_DESC_SIZE : 0@JOB_VA + i * JOB_DESC_SIZE : 0
no-clean@mali_sched.c@mali_bo_for_gpu(j->bos\[i\]);@;
no-inval@mali_sched.c@mali_bo_for_cpu(j->bos\[i\]);@;
new-bo-dirty@mali_bo.c@cache(CACHE_CLEAN_INVALIDATE, bo->cpu, size);@;
map-no-cache@mali_bo.c@	bo->cpu_access = 1;@	;
flush-red@mali_sched.c@JS_CONFIG_ENABLE_FLUSH_REDUCTION |@
no-disable@mali_mmu.c@^	as_disable(as);$@	;
no-enable@mali_mmu.c@if (as_enable(pick, vm) != OK) {@if (0) {
no-unmask@mali_mmu.c@mali_rd(gpu(), MMU_INT_MASK) |@mali_rd(gpu(), MMU_INT_MASK) \&
no-mask@mali_mmu.c@~MMU_INT_AS(as));@MMU_INT_AS(as));
no-await-mmu@mali_sched.c@else if (j->as >= 0 \&\& (mali_rd(gpu(), MMU_INT_RAWSTAT) \&@else if (0 \&\& (mali_rd(gpu(), MMU_INT_RAWSTAT) \&
mmu-half-alone@mali_sched.c@j->as == as \&\& j->await_mmu) {@j->as == as) {
fault-va@mali_job.c@#define FAULT_OFF	0x10040u@#define FAULT_OFF	0x1040u
exc-mask@malireg.h@AS_FAULT_EXCEPTION(x)		((x) \& 0xff)@AS_FAULT_EXCEPTION(x)		((x) \& 0x7f)
access-shift@malireg.h@(((x) >> 8) \& 3)@(((x) >> 9) \& 3)
asked@mali_job.c@st.vm.expect_fault = run == RUN_FAULT;@st.vm.expect_fault = 1;
contig-pages@mali_mmu.c@(pages\[i + k\] \& MALI_PTE_ADDR)@((pages[i] + k * MALI_PAGE) \& MALI_PTE_ADDR)
no-flush-unmap@mali_mmu.c@if (vm->as >= 0 \&\& npages > 0)@if (0)
lock-width@mali_mmu.c@64 - (unsigned)__builtin_clzll(diff)@63 - (unsigned)__builtin_clzll(diff)
align-code@mali_bo.c@		align = size;@		align = 0;
align-2m@mali_bo.c@align = size >= SZ_2M ? SZ_2M : 0;@align = 0;
fifo@mali_sched.c@continue;	/\* an earlier job of its own is not over \*/@;
close-frees-early@mali_drm.c@if (c->state == CTX_CLOSING \&\& c->jobs == 0)@if (c->state == CTX_CLOSING)
close-leaks@mali_drm.c@mali_bo_unref((struct mali_bo \*)c->bo\[i\]);@;
vircopy-vfs@mali_drm.c@sys_vircopy(c->user, (vir_bytes)ptr@sys_vircopy(c->endpt, (vir_bytes)ptr
wait-bo-past@mali_drm.c@		return EBUSY;@		return ETIMEDOUT;
fence-ok@mali_drm.c@p->out->error = j->result == OK ? 0 : EIO;@p->out->error = 0;
wait-error@mali_drm.c@return f->error != 0 ? EIO : OK;@return OK;
no-out-fence@mali_drm.c@		syncobj_set(out, p->out);@		;
mmap-part@mali_drm.c@if (len != bo->size ||@if (
version-name@mali_drm.c@"panfrost", \&v->name_len@"mali", \&v->name_len
remap-self@mali_bo.c@v = vm_remap(who, sef_self(),@v = vm_remap(who, SELF,
param-rev@mali_drm.c@return mali.gpu_id \& 0xffff;@return mali.gpu_id >> 12;
'

echo "$MUTATIONS" | while IFS='@' read -r name file from to; do
	[ -n "$name" ] || continue
	rm -rf "$WORK"
	cp -r "$ORIG" "$WORK"
	sed -i "s%$from%$to%" "$WORK/$file"
	if cmp -s "$ORIG/$file" "$WORK/$file"; then
		echo "НЕ ПРИМЕНИЛАСЬ  $name"
		continue
	fi
	if SRC=$WORK OUT=$WORK sh "$HERE/run.sh" >"$WORK/out" 2>&1; then
		echo "ПРОПУЩЕНА       $name"
	else
		echo "поймана         $name ($(grep -c ОТКАЗ "$WORK/out") отказ.)"
	fi
done
