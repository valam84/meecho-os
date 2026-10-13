#!/bin/sh
# Проверка стенда: каждая мутация - одна правдоподобная ошибка в копии
# драйвера, и стенд обязан её поймать.  Стенд, который проходит на
# испорченном драйвере, ничего не доказывает о целом.
#
#	sh mutate.sh
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
ORIG=$TREE/minix/drivers/video/mali
WORK=${WORK:-/tmp/mali-mutate}

# имя@файл@что заменить@на что (для sed: & писать как \&, * как \*)
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
leaf-type@mali_job.c@MALI_PTE_SH_OUTER | MALI_PTE_LEAF;@MALI_PTE_SH_OUTER | MALI_PTE_TABLE;
leaf-write@mali_job.c@MALI_PTE_READ | MALI_PTE_WRITE |@MALI_PTE_READ |
l2-shift@mali_job.c@i2 = (va >> 21)@i2 = (va >> 20)
read-inner@mali_job.c@AS_TRANSTAB_READ_INNER;@0;
memattr@malireg.h@0x00888d88u@0x008d8888u
wv-value@malireg.h@JOB_WV_VALUE[[:space:]]*48@JOB_WV_VALUE 40
job-index@mali_job.c@JOB_CTL_INDEX(i + 1)@JOB_CTL_INDEX(i)
next-ptr@mali_job.c@JOB_VA + (i + 1) \* JOB_DESC_SIZE : 0@JOB_VA + i * JOB_DESC_SIZE : 0
no-clean@mali_job.c@cache(CACHE_CLEAN_INVALIDATE, DESC, NBUF \* PAGE, "the job buffer");@;
no-inval@mali_job.c@cache(CACHE_INVALIDATE, DESC, NBUF \* PAGE, "the job buffer");@;
flush-red@mali_job.c@JS_CONFIG_ENABLE_FLUSH_REDUCTION |@
no-disable@mali_job.c@^	as_disable();$@	;
no-reenable@mali_job.c@if (as_enable() != OK) {@if (0) {
no-unmask@mali_job.c@mali_rd(gpu(), MMU_INT_MASK) |@mali_rd(gpu(), MMU_INT_MASK) \&
no-mask@mali_job.c@~MMU_INT_AS(AS_JOBS));@MMU_INT_AS(AS_JOBS));
recover-early@mali_job.c@	if (mj.failed)@	if (1)
recover-alone@mali_job.c@		if (mj.faulted)@		if (1)
fault-va@mali_job.c@(JOB_VA + 0x10040u)@(JOB_VA + 0x1040u)
fault-clean@mali_job.c@cache(CACHE_CLEAN_INVALIDATE, DESC, JOB_DESC_SIZE, "the fault job");@;
exc-mask@malireg.h@AS_FAULT_EXCEPTION(x)		((x) \& 0xff)@AS_FAULT_EXCEPTION(x)		((x) \& 0x7f)
access-shift@malireg.h@(((x) >> 8) \& 3)@(((x) >> 9) \& 3)
asked@mali_job.c@asked = mj.busy \&\& mj.run == RUN_FAULT;@asked = 1;
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
