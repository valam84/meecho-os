#!/bin/sh
# Snapshot of a running postmarketOS on the Mi Pad 4 (clover) for MEECHO:
# what the working system does on this hardware.  Only reads.
# Run as root for the full picture (iomem, debugfs, dmesg); as a user it
# takes what it can.  Output: $OUT (default /tmp/meecho-cap).
OUT=${OUT:-/tmp/meecho-cap}
rm -rf "$OUT"; mkdir -p "$OUT"
cd "$OUT" || exit 1
c() { # c FILE CMD... : run, keep stdout+stderr
	f=$1; shift
	mkdir -p "$(dirname "$f")"
	{ echo "# $*"; "$@"; } >"$f" 2>&1
}
cf() { # cf FILE SRC : copy a file if readable
	mkdir -p "$(dirname "$1")"; cat "$2" >"$1" 2>&1
}
dumpdir() { # dumpdir OUTFILE DIR [maxdepth] : every readable attribute
	find "$2" -maxdepth "${3:-3}" -type f 2>/dev/null | sort | while read -r f; do
		case "$f" in */uevent|*/power/*|*/subsystem/*) ;; esac
		v=$(head -c 4096 "$f" 2>/dev/null | tr '\0' ' ') || continue
		printf '%s = %s\n' "$f" "$v"
	done >"$1"
}

# --- general
c sys/uname.txt uname -a
cf sys/os-release.txt /etc/os-release
cf sys/cmdline.txt /proc/cmdline
cf sys/cpuinfo.txt /proc/cpuinfo
cf sys/meminfo.txt /proc/meminfo
cf sys/iomem.txt /proc/iomem
cf sys/interrupts.txt /proc/interrupts
cf sys/modules.txt /proc/modules
c sys/dmesg.txt dmesg
c sys/lsmod.txt lsmod
c sys/ps.txt ps -o pid,user,args
c sys/rc-status.txt rc-status -a
c sys/services.txt sh -c 'ls /etc/init.d /etc/runlevels/*'
c sys/apk-installed.txt apk info -v
cf sys/config.gz /proc/config.gz
c sys/partlabels.txt ls -l /dev/disk/by-partlabel
c sys/lsblk.txt sh -c 'lsblk -o NAME,SIZE,TYPE,PARTLABEL,MOUNTPOINT 2>/dev/null || cat /proc/partitions'
c sys/mounts.txt cat /proc/mounts

# --- device tree: the running one, whole
cf dt/fdt.dtb /sys/firmware/fdt
if command -v dtc >/dev/null; then
	c dt/live.dts dtc -I fs -O dts /sys/firmware/devicetree/base
	[ -s dt/fdt.dtb ] && c dt/fdt.dts dtc -I dtb -O dts dt/fdt.dtb
fi
tar cf dt/devicetree-base.tar -C /sys/firmware devicetree 2>/dev/null

# --- firmware: names and digests (copies are taken separately)
c fw/firmware-list.txt sh -c 'find /lib/firmware -type f -o -type l | sort | while read f; do printf "%s %s %s\n" "$(sha256sum "$f" 2>/dev/null | cut -c1-64)" "$(stat -L -c %s "$f" 2>/dev/null)" "$f"; done'
c fw/firmware-links.txt sh -c 'find /lib/firmware -type l -exec ls -l {} \;'
c fw/apk-firmware.txt sh -c 'apk info -v | grep -i -E "firmware|msm|qcom|clover|rmtfs|tqftp|pd-mapper|qrtr|modem"'
c fw/apk-firmware-files.txt awk '/^P:/{p=$0} /^F:/{d=substr($0,3)} /^R:/{if (p ~ /firmware|clover|rmtfs|tqftp|pd-mapper|qrtr|hexagon|modem|soc-qcom|sdm660|alsa-ucm/) print p, d "/" substr($0,3)}' /lib/apk/db/installed
c fw/firmware-search.txt sh -c 'cat /sys/module/firmware_class/parameters/path; echo; ls -l /lib/firmware; mount | grep -i -E "firmware|modem|vendor|persist|dsp|bt_firmware"; find / -xdev \( -name "*.mbn" -o -name "*.mdt" -o -name "*.b0[0-9]" -o -name "bdwlan*" -o -name "board-2.bin" -o -name "firmware-5.bin" -o -name "crnv*" -o -name "crbtfw*" -o -name "*.jsn" \) 2>/dev/null | head -300'

# --- remote processors, rpmsg, qrtr (modem, wifi, audio, sensors live here)
dumpdir rproc/remoteproc.txt /sys/class/remoteproc 2
c rproc/remoteproc-ls.txt sh -c 'for r in /sys/class/remoteproc/*; do echo "== $r $(cat $r/name) $(cat $r/state) $(cat $r/firmware 2>/dev/null)"; readlink -f $r; done'
c rproc/rpmsg.txt sh -c 'for d in /sys/bus/rpmsg/devices/*; do echo "$(basename $d) drv=$(basename $(readlink $d/driver) 2>/dev/null) name=$(cat $d/name 2>/dev/null) src=$(cat $d/src 2>/dev/null) dst=$(cat $d/dst 2>/dev/null)"; done'
c rproc/qrtr-lookup.txt qrtr-lookup
c rproc/qrtr-cfg.txt sh -c 'qrtr-cfg 2>&1; cat /proc/net/qrtr 2>&1'
c rproc/smem-smp2p.txt sh -c 'ls -l /sys/bus/platform/drivers/qcom-smp2p /sys/bus/platform/drivers/qcom_smsm /sys/bus/platform/drivers/qcom-smem 2>&1'
c rproc/glink-smd.txt sh -c 'ls -lR /sys/bus/platform/drivers | grep -i -E "smd|glink|mss|adsp|cdsp|slpi|pil|q6v5|rmtfs|wcn|wifi|ipa|qmi"'
c rproc/rmtfs.txt sh -c 'ls -l /dev/qcom_rmtfs* /sys/class/rmtfs 2>&1; cat /proc/$(pidof rmtfs)/cmdline 2>/dev/null | tr "\0" " "; echo; ls -l /dev/disk/by-partlabel | grep -i -E "modemst|fsg|fsc|persist|modem|dsp|bluetooth"'
c rproc/daemons.txt sh -c 'for p in rmtfs tqftpserv pd-mapper qrtr-ns ModemManager iwd wpa_supplicant NetworkManager hexagonrpcd msm-modem-uim-selection; do echo "== $p: $(pidof $p)"; done'

# --- Wi-Fi (WCN3990 via ath10k_snoc; firmware runs in the modem)
c wifi/iw-dev.txt iw dev
c wifi/iw-phy.txt iw phy
c wifi/iw-reg.txt iw reg get
c wifi/ip.txt ip -d addr
c wifi/nmcli.txt nmcli -f all dev show
c wifi/link.txt sh -c 'for i in /sys/class/net/wlan*; do iw dev $(basename $i) link; iw dev $(basename $i) station dump; done'
c wifi/ethtool.txt sh -c 'for i in /sys/class/net/wlan*; do ethtool -i $(basename $i); done'
dumpdir wifi/sysfs-net.txt /sys/class/net/wlan0/ 2
c wifi/snoc-device.txt sh -c 'd=$(readlink -f /sys/class/net/wlan0/device); echo $d; ls -l $d; ls -l $d/driver; cat $d/of_node/compatible | tr "\0" " "'
c wifi/dmesg.txt sh -c 'dmesg | grep -i -E "ath10k|wcn|wlan|wlfw|snoc|qmi|80211|cfg80211"'
c wifi/debugfs-ls.txt sh -c 'ls -lR /sys/kernel/debug/ieee80211 2>&1 | head -200'
for f in fw_stats tpc_stats fw_dbglog chip_id fw_checksums wmi_services htt_stats_mask; do
	for d in /sys/kernel/debug/ieee80211/phy*/ath10k; do
		[ -e "$d/$f" ] && cf wifi/debugfs/$f.txt "$d/$f"
	done
done
c wifi/persist.txt sh -c 'ls -lR /lib/firmware/ath10k /lib/firmware/qcom/sdm660 /lib/firmware/postmarketos 2>&1'

# --- modem (MSS + IPA + QMI over qrtr)
c modem/mmcli-L.txt mmcli -L
c modem/mmcli-m.txt mmcli -m any
c modem/mmcli-m-full.txt sh -c 'mmcli -m any --output-keyvalue'
c modem/mmcli-sim.txt sh -c 'mmcli -m any --sim any 2>&1 || mmcli -i any'
c modem/mmcli-3gpp.txt sh -c 'mmcli -m any --location-status; mmcli -m any --signal-get'
c modem/qmicli.txt sh -c 'for d in /dev/wwan*qmi* /dev/cdc-wdm*; do [ -e $d ] && { echo "== $d"; qmicli -d $d --dms-get-ids; qmicli -d $d --dms-get-revision; qmicli -d $d --dms-get-capabilities; qmicli -d $d --nas-get-serving-system; }; done; qmicli --help-all >/dev/null 2>&1 && echo qmicli-present'
c modem/qmi-over-qrtr.txt sh -c 'qmicli -d qrtr://0 --dms-get-revision; qmicli -d qrtr://0 --dms-get-ids; qmicli -d qrtr://0 --dms-get-capabilities; qmicli -d qrtr://0 --dms-get-operating-mode; qmicli -d qrtr://0 --nas-get-serving-system; qmicli -d qrtr://0 --nas-get-signal-info; qmicli -d qrtr://0 --wda-get-data-format'
c modem/wwan.txt sh -c 'ls -l /dev/wwan* /sys/class/wwan 2>&1; ip -d link show; ls -l /sys/class/net'
c modem/ipa.txt sh -c 'ls -l /sys/bus/platform/drivers/ipa* 2>&1; for i in /sys/class/net/rmnet* /sys/class/net/*ipa*; do [ -e $i ] && { echo $i; ip -d link show $(basename $i); cat $i/device/of_node/compatible 2>/dev/null|tr "\0" " "; echo; }; done'
c modem/dmesg.txt sh -c 'dmesg | grep -i -E "mss|modem|q6v5|ipa|rmnet|qmi|rmtfs|wwan|mba|remoteproc|pil|smp2p|sysmon|pdr|qrtr"'
c modem/mm-log.txt sh -c 'tail -n 400 /var/log/messages 2>&1 | grep -i -E "modemmanager|qmi|mm-|rmtfs"'
c modem/modem-nodes.txt sh -c 'ls -l /dev/qrtr* /dev/smd* /dev/rpmsg* /dev/qcom* /dev/fastrpc* 2>&1'

# --- audio, bluetooth, sensors, power, input, misc
c audio/aplay.txt sh -c 'cat /proc/asound/cards; aplay -l; amixer -c0 contents 2>&1 | head -400'
c audio/ucm.txt sh -c 'ls -lR /usr/share/alsa/ucm2 2>/dev/null | grep -i -E "sdm660|clover|xiaomi" ; find /usr/share/alsa/ucm2 -ipath "*clover*" -o -ipath "*sdm660*" | head'
c bt/hci.txt sh -c 'hciconfig -a 2>&1; bluetoothctl show 2>&1 | head -30; dmesg | grep -i -E "bluetooth|hci|btqca|wcn"'
dumpdir power/power_supply.txt /sys/class/power_supply 2
dumpdir power/thermal.txt /sys/class/thermal 2
c power/regulators.txt sh -c 'for r in /sys/class/regulator/*; do echo "$(basename $r) $(cat $r/name) en=$(cat $r/state 2>/dev/null) uV=$(cat $r/microvolts 2>/dev/null) users=$(cat $r/num_users)"; done'
c power/cpufreq.txt sh -c 'for p in /sys/devices/system/cpu/cpufreq/policy*; do echo "== $p"; grep . $p/scaling_driver $p/scaling_cur_freq $p/scaling_available_frequencies $p/scaling_governor $p/related_cpus 2>/dev/null; done; grep . /sys/devices/system/cpu/cpu*/cpuidle/state*/name 2>/dev/null'
c power/rtc.txt sh -c 'ls -l /sys/class/rtc; cat /sys/class/rtc/rtc0/name /proc/driver/rtc; hwclock -r'
c power/wakeup.txt sh -c 'cat /sys/power/state /sys/power/mem_sleep 2>&1'
c input/devices.txt cat /proc/bus/input/devices
c input/evdev.txt sh -c 'for e in /sys/class/input/event*; do echo "$(basename $e) $(cat $e/device/name) $(readlink -f $e/device)"; done'
dumpdir sensors/iio.txt /sys/bus/iio/devices 2
c sensors/dmesg.txt sh -c 'dmesg | grep -i -E "slpi|smgr|sns|iio|sensor|accel|gyro|prox|mag"'
dumpdir leds/leds.txt /sys/class/leds 2
dumpdir misc/backlight.txt /sys/class/backlight 2
c misc/drm.txt sh -c 'ls -l /sys/class/drm; for c in /sys/class/drm/card*-*; do echo "$c $(cat $c/status) $(cat $c/modes 2>/dev/null | head -2)"; done'
c misc/usb.txt sh -c 'ls -l /sys/class/udc /sys/kernel/config/usb_gadget 2>&1; lsusb 2>&1; cat /sys/class/typec/*/data_role 2>&1'
c misc/gpio.txt sh -c 'cat /sys/kernel/debug/gpio 2>&1; ls /sys/class/gpio'
c misc/pinctrl.txt sh -c 'ls /sys/kernel/debug/pinctrl 2>&1; for d in /sys/kernel/debug/pinctrl/*; do echo "== $d"; cat $d/pinmux-pins 2>/dev/null | grep -v UNCLAIMED; done'
c misc/platform-drivers.txt sh -c 'for d in /sys/bus/platform/devices/*; do dr=$(readlink $d/driver); [ -n "$dr" ] && echo "$(basename $d) $(basename $dr)"; done | sort'
c misc/i2c.txt sh -c 'for d in /sys/bus/i2c/devices/*; do echo "$(basename $d) $(cat $d/name 2>/dev/null) $(basename $(readlink $d/driver) 2>/dev/null)"; done'
c misc/spmi.txt sh -c 'for d in /sys/bus/spmi/devices/*; do echo "$(basename $d) $(basename $(readlink $d/driver) 2>/dev/null)"; done'
c misc/venus.txt sh -c 'v4l2-ctl --list-devices 2>&1; dmesg | grep -i venus'

# --- root only: clocks, regulators, interconnect, regmaps
if [ "$(id -u)" = 0 ]; then
	cf debug/clk_summary.txt /sys/kernel/debug/clk/clk_summary
	cf debug/regulator_summary.txt /sys/kernel/debug/regulator/regulator_summary
	cf debug/interconnect_summary.txt /sys/kernel/debug/interconnect/interconnect_summary
	cf debug/devices_deferred.txt /sys/kernel/debug/devices_deferred
	cf debug/qcom_rpm.txt /sys/kernel/debug/rpm_stats
	c debug/regmap-ls.txt ls /sys/kernel/debug/regmap
	c debug/remoteproc.txt sh -c 'for r in /sys/kernel/debug/remoteproc/*; do echo "== $r"; for f in name resource_table carveout_memories; do [ -e $r/$f ] && { echo "-- $f"; cat $r/$f; }; done; done'
	c debug/smem.txt sh -c 'ls -lR /sys/kernel/debug/qcom* 2>&1 | head -80'
fi
echo done >DONE
