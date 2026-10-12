#!/bin/sh
# Стенд sunxi против испорченного драйвера: каждая мутация обязана уронить
# прогон. Стенд, который проходит на всём, ничего не доказывает.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../../.." && pwd)
WORK=${WORK:-$HOME/sunxi-mutate}

mutate() {
	name=$1; expr=$2
	rm -rf "$WORK"
	mkdir -p "$WORK"
	cp -r "$TREE/minix" "$TREE/port" "$WORK/"
	sed -i "$expr" "$WORK/minix/drivers/storage/mmc/sdmmc_sunxi.c"
	if cmp -s "$TREE/minix/drivers/storage/mmc/sdmmc_sunxi.c" \
	    "$WORK/minix/drivers/storage/mmc/sdmmc_sunxi.c"; then
		echo "mutation $name: did not apply"
		return
	fi
	if TREE="$WORK" OUT="$WORK/obj" QEMU_TIMEOUT=60 \
	    sh "$WORK/port/test/sunxi/run.sh" > "$WORK/out.txt" 2>&1; then
		echo "mutation $name: NOT CAUGHT"
	else
		echo "mutation $name: caught ($(grep -c FAIL "$WORK/out.txt") FAIL lines, $(grep -o '[0-9]* bad' "$WORK/out.txt" | tr '\n' ' '))"
	fi
}

# FIFO по старому адресу A10: программный путь читает не тот регистр.
mutate fifo 's/{ "allwinner,sun50i-a64-mmc",\t0, 1, 1, 1, SX_FIFO }/{ "allwinner,sun50i-a64-mmc",\t0, 1, 1, 1, SX_FIFO_OLD }/'
# Без автостопа: многоблочная передача без CMD23 не заканчивается.
mutate autostop 's/cmdreg |= SX_CMD_SEND_STOP;/(void)0;/'
# Адреса дескрипторов со сдвигом H616 на контроллере A64: движок пишет не
# туда. (Снятый флаг «последний дескриптор» здесь не ловится вовсе: модель
# QEMU останавливает DMA по счётчику байт, а флаг не читает. Это предел
# модели, а не стенда - на железе такую ошибку искать на железе.)
mutate shift 's/{ "allwinner,sun50i-a64-mmc",\t0, 1, 1, 1, SX_FIFO }/{ "allwinner,sun50i-a64-mmc",\t2, 1, 1, 1, SX_FIFO }/'
# Ответ R2 не целиком: CID и CSD сдвинуты.
mutate r2 's/cmd->resp\[i\] = rd(SX_RESP0 + 4 \* i);/cmd->resp[i] = rd(SX_RESP0 + 4 * i) >> 8;/'
