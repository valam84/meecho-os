#!/bin/sh
# Проверка обработки резервов памяти на QEMU, у которого их нет.
#
# У virt-машины ни /reserved-memory, ни memreserve-блока, поэтому код,
# написанный ради CB2, иначе остался бы непроверенным до первого запуска на
# плате - то есть до момента, когда отличить его отказ от чужого труднее
# всего. Здесь дерево снимается с самой машины, в него добавляются оба вида
# резерва, и ядро запускается с ним.
#
# Ожидание: в карте памяти появляются две дыры - 0x50000000-0x51000000
# (memreserve) и 0x52000000-0x52800000 (/reserved-memory).
set -e

W=$HOME/obj-evbarm64/work
T=/tmp/rsvtest
mkdir -p $T

qemu-system-aarch64 -M virt,dumpdtb=$T/virt.dtb -cpu cortex-a72 -m 512 \
	-display none -net none >/dev/null 2>&1
dtc -I dtb -O dts -o $T/virt.dts $T/virt.dtb 2>/dev/null

# memreserve идёт до корня дерева, отдельной строкой формата.
sed -i '1a /memreserve/ 0x50000000 0x1000000;' $T/virt.dts

# /reserved-memory — узел корня; ширины ячеек те же, что у корня virt.
sed -i 's|^\tchosen {|\treserved-memory {\n\t\t#address-cells = <0x02>;\n\t\t#size-cells = <0x02>;\n\t\tranges;\n\t\tmeecho-test@52000000 {\n\t\t\treg = <0x00 0x52000000 0x00 0x800000>;\n\t\t\tno-map;\n\t\t};\n\t};\n\n\tchosen {|' $T/virt.dts

dtc -I dts -O dtb -o $T/virt-rsv.dtb $T/virt.dts 2>/dev/null
echo "--- что добавлено ---"
grep -n "memreserve\|meecho-test\|reg = <0x00 0x52" $T/virt.dts | head

echo "--- запуск с этим деревом ---"
timeout 20 qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512 -smp 1 \
	-display none -serial stdio -net none \
	-kernel $W/kernel.bin -initrd $W/boot.mba -dtb $T/virt-rsv.dtb \
	-append "bootramdisk=1 console=tty00" 2>&1 | head -6
