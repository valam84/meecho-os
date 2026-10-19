#!/bin/sh
# Second pass (root): what the first one missed or could not do.
OUT=${OUT:-/tmp/meecho-cap2}
rm -rf "$OUT"; mkdir -p "$OUT"; cd "$OUT" || exit 1
c() { f=$1; shift; mkdir -p "$(dirname "$f")"; { echo "# $*"; "$@"; } >"$f" 2>&1; }
dumpdir() {
	mkdir -p "$(dirname "$1")"
	find "$2" -maxdepth "${3:-3}" \( -type f -o -type l \) 2>/dev/null | sort | while read -r f; do
		[ -f "$f" ] || continue
		v=$(head -c 2048 "$f" 2>/dev/null | tr '\0\n' '  ') || continue
		printf '%s = %s\n' "$f" "$v"
	done >"$1"
}
dumpdir power/power_supply.txt /sys/class/power_supply/ 2
dumpdir power/thermal.txt /sys/class/thermal/ 2
dumpdir rproc/remoteproc-sysfs.txt /sys/class/remoteproc/ 2
dumpdir leds/leds.txt /sys/class/leds/ 2
dumpdir misc/backlight.txt /sys/class/backlight/ 2
dumpdir sensors/iio.txt /sys/bus/iio/devices/ 2
dumpdir wifi/sysfs-net.txt /sys/class/net/wlan0/ 1
dumpdir wifi/ieee80211.txt /sys/class/ieee80211/ 2
dumpdir modem/rmnet-sysfs.txt /sys/class/net/rmnet_ipa0/ 1
dumpdir modem/ipa_lan0-sysfs.txt /sys/class/net/ipa_lan0/ 1
c modem/module-params.txt sh -c 'for m in ipa2_lite ath10k_core ath10k_snoc qcom_q6v5_mss qcom_q6v5_pas rmtfs_mem qrtr qrtr_smd qcom_common qcom_sysmon qcom_glink_smem qcom_smd qcom_smp2p qcom_smem qcom_pd_mapper mac80211 cfg80211 hci_uart btqca qcom_smgr sns_smgr qcom_sns_reg fastrpc; do [ -d /sys/module/$m ] || continue; echo "== $m $(cat /sys/module/$m/version /sys/module/$m/srcversion 2>/dev/null)"; grep . /sys/module/$m/parameters/* 2>/dev/null; done'
c modem/modinfo.txt sh -c 'for m in ipa2_lite ath10k_snoc ath10k_core qcom_q6v5_mss rmtfs_mem qrtr_smd qcom_pd_mapper btqca hci_uart qcom_smgr sns_smgr qcom_sns_reg; do modinfo $m; echo; done'
c sys/systemd-units.txt systemctl list-units --all --no-pager
c sys/systemd-enabled.txt systemctl list-unit-files --no-pager --state=enabled
c sys/units-cat.txt sh -c 'for u in rmtfs tqftpserv pd-mapper qrtr-ns ModemManager msm-firmware-loader msm-firmware-loader-unpack hexagonrpcd q6voiced feedbackd iio-sensor-proxy bluetooth; do systemctl cat $u --no-pager 2>/dev/null; done'
for u in rmtfs tqftpserv ModemManager NetworkManager msm-firmware-loader wpa_supplicant bluetooth; do
	c journal/$u.txt journalctl -b -u $u --no-pager -o short-monotonic
done
c journal/kernel-firmware.txt sh -c 'journalctl -b -k --no-pager -o short-monotonic | grep -i -E "firmware|remoteproc|q6v5|mss|mba|rmtfs|tqftp|pil|wlfw|ath10k|ipa"'
c journal/all-boot-head.txt sh -c 'journalctl -b --no-pager -o short-monotonic | head -3000'
c fw/msm-firmware-loader.txt sh -c 'for f in $(command -v msm-firmware-loader msm-firmware-loader-unpack 2>/dev/null) /usr/libexec/msm-firmware-loader* /usr/lib/msm-firmware-loader* /usr/share/msm-firmware-loader*; do [ -f "$f" ] && { echo "==== $f"; cat "$f"; }; done; ls -la /run/msm-firmware-loader /run/msm-firmware-loader/mnt'
c fw/target-ls.txt ls -l /run/msm-firmware-loader/target /run/msm-firmware-loader/target/qcom
c fw/modem-image-ls.txt sh -c 'cd /run/msm-firmware-loader/mnt/modem && find . -type f | sort | xargs sha256sum; ls -lR'
c fw/dsp-ls.txt sh -c 'cd /run/msm-firmware-loader/mnt/dsp && find . -type f | sort | xargs sha256sum'
c fw/bluetooth-ls.txt sh -c 'cd /run/msm-firmware-loader/mnt/bluetooth && find . -type f | sort | xargs sha256sum'
c fw/vendor-firmware-ls.txt sh -c 'cd /run/msm-firmware-loader/mnt/vendor && ls -lR firmware firmware_mnt 2>&1; ls etc | head -200'
c fw/persist-ls.txt sh -c 'cd /run/msm-firmware-loader/mnt/persist && find . | sort | xargs ls -ld'
c sys/partitions.txt sh -c 'for p in /sys/block/mmcblk1/mmcblk1p*; do n=$(basename $p); echo "$n start=$(cat $p/start) size=$(cat $p/size) $(grep PARTNAME $p/uevent)"; done; cat /sys/block/mmcblk1/size'
c sys/firmware-requests.txt sh -c 'dmesg | grep -i -E "direct firmware|firmware: |loading.*firmware|request_firmware"'
c rproc/rmtfs-mem.txt sh -c 'cat /proc/iomem | grep -i -E "rmtfs|85e0|wlan|msa|mpss|mba|adsp|cdsp|ipa|smem|removed"; ls -l /sys/devices/platform/85e00000.memory/rmtfs/qcom_rmtfs_mem1/; cat /sys/devices/platform/85e00000.memory/rmtfs/qcom_rmtfs_mem1/phys_addr /sys/devices/platform/85e00000.memory/rmtfs/qcom_rmtfs_mem1/size 2>&1'
c rproc/qrtr-services.txt sh -c 'for s in 1 2 3 5 9 10 11 12 15 16 26 41 42 43 45 47 48 64 66 69 76; do :; done; qmicli -d qrtr://0 --get-service-version-info 2>&1; for n in 0 1 5; do echo "== node $n"; qmicli -d qrtr://$n --get-service-version-info 2>&1 | head -60; done'
c modem/nas.txt sh -c 'qmicli -d qrtr://0 --nas-get-system-info; qmicli -d qrtr://0 --nas-get-rf-band-info; qmicli -d qrtr://0 --nas-get-cell-location-info | head -40; qmicli -d qrtr://0 --uim-get-card-status; qmicli -d qrtr://0 --wds-get-packet-service-status; qmicli -d qrtr://0 --wda-get-data-format; qmicli -d qrtr://0 --dms-get-software-version; qmicli -d qrtr://0 --dms-get-band-capabilities'
c modem/mmcli-signal.txt sh -c 'mmcli -m any --signal-setup=5; sleep 6; mmcli -m any --signal-get; mmcli -m any --3gpp-scan --timeout=60 2>&1 | head -20'
c wifi/scan.txt sh -c 'nmcli dev wifi rescan; sleep 8; nmcli -f SSID,BSSID,CHAN,FREQ,RATE,SIGNAL,SECURITY dev wifi list'
c wifi/iw-like.txt sh -c 'cat /proc/net/wireless; cat /sys/class/net/wlan0/address; ls -l /sys/class/net/wlan0/device/; cat /sys/class/net/wlan0/device/of_node/compatible | tr "\0" " "'
c bt/journal-bt.txt sh -c 'journalctl -b --no-pager | grep -i -E "bluetooth|hci0|btqca" | head -100; bluetoothctl show 2>&1 | head -40'
c misc/interrupts-after.txt cat /proc/interrupts
c sys/proc-cpu.txt sh -c 'cat /sys/devices/system/cpu/possible /sys/devices/system/cpu/online; for p in /sys/devices/system/cpu/cpufreq/policy*; do echo "== $p"; grep . $p/scaling_driver $p/scaling_cur_freq $p/scaling_available_frequencies $p/scaling_governor $p/related_cpus 2>/dev/null; done'
c audio/asound.txt sh -c 'cat /proc/asound/cards /proc/asound/pcm; ls /usr/share/alsa/ucm2/conf.d; find /usr/share/alsa/ucm2 -iname "*clover*" -o -iname "*sdm660*" | while read f; do echo "== $f"; ls -l "$f"; done'
c input/evdev.txt sh -c 'for e in /sys/class/input/event*; do echo "$(basename $e) $(cat $e/device/name) $(readlink -f $e/device)"; done; cat /proc/bus/input/devices'
c misc/udev-net.txt sh -c 'for i in wlan0 rmnet_ipa0 ipa_lan0 usb0; do echo "== $i"; udevadm info /sys/class/net/$i; done'
c misc/udev-props.txt sh -c 'udevadm info /dev/qcom_rmtfs_mem1; udevadm info /sys/class/remoteproc/remoteproc0'
echo done >DONE
