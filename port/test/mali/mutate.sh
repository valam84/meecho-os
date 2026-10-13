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
div-field@mali_rk.c@((0x800u | 0xfu) << 16) | (div - 1)@((0x800u | 0xfu) << 16) | div
safe-rate@mali_rk.c@#define MALI_SAFE_KHZ		400000@#define MALI_SAFE_KHZ		600000
gate-mask@mali_rk.c@want << 16);@want);
pll-postdiv@mali_rk.c@pd2 = (con1 >> 6) \& 0x7;@pd2 = (con1 >> 6) \& 0x3;
irq-order@mali_find.c@"gpu", "job", "mmu"@"gpu", "mmu", "job"
clock-cells@mali_find.c@p + idx \* 8 + 4@p + idx \* 8
supply-walk@mali_find.c@(void)fdt_walk(dtb, find_supply, info);@;
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
