OUT=/tmp/meecho-cap3; rm -rf $OUT; mkdir -p $OUT; cd $OUT
d() { for e in $2/*; do n=$(basename $e); for f in $e/*; do [ -f "$f" ] && [ -r "$f" ] || continue; case $f in */uevent|*/modalias) continue;; esac; v=$(head -c 1024 "$f" 2>/dev/null | tr '\0\n' '  '); echo "$n/$(basename $f) = $v"; done; done > $1 2>/dev/null; }
d power_supply.txt /sys/class/power_supply
d thermal.txt /sys/class/thermal
d remoteproc.txt /sys/class/remoteproc
d leds.txt /sys/class/leds
d backlight.txt /sys/class/backlight
d iio.txt /sys/bus/iio/devices
d ieee80211.txt /sys/class/ieee80211
d regulator.txt /sys/class/regulator
d rtc.txt /sys/class/rtc
d typec.txt /sys/class/typec
d udc.txt /sys/class/udc
d hwmon.txt /sys/class/hwmon
cat /sys/kernel/debug/gpio > gpio.txt 2>&1
for p in /sys/kernel/debug/pinctrl/*; do echo "== $p"; cat $p/pinmux-pins 2>/dev/null | grep -v UNCLAIMED; done > pinmux.txt 2>&1
cat /sys/kernel/debug/qcom_socinfo/* > socinfo.txt 2>/dev/null; for f in /sys/devices/soc0/*; do [ -f $f ] && echo "$(basename $f) = $(cat $f)"; done >> socinfo.txt 2>/dev/null
cat /sys/firmware/devicetree/base/chosen/bootargs 2>/dev/null | tr '\0' '\n' > chosen.txt
