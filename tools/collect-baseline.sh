#!/bin/bash
# Collect a device baseline snapshot, to diff after kernel/rootfs changes.
#
# Usage: tools/collect-baseline.sh [outfile] [dmesg-outfile]
# Requires the device on adb (Ubuntu slot).  Defaults write into docs/.
set +e
ADB=${ADB:-adb}
OUT=${1:-docs/baseline-$(date +%F).txt}
DMESG_OUT=${2:-${OUT%.txt}-dmesg.txt}

run() { $ADB shell "$1" 2>/dev/null; }

{
	echo
	echo "##### DATE #####"
	date -u
	run "date -u; uptime; uname -a"

	echo
	echo "##### CMDLINE #####"
	run "cat /proc/cmdline"

	echo
	echo "##### SLOT #####"
	run "sudo qbootctl -a 2>/dev/null | head -3"

	echo
	echo "##### SYSTEMD #####"
	run "systemctl is-system-running"

	echo
	echo "##### SERVICES_RUNNING #####"
	run "systemctl list-units --type=service --state=running --no-legend --no-pager | awk '{print \$1}' | sort"

	echo
	echo "##### SERVICES_FAILED #####"
	run "systemctl --failed --no-legend --no-pager"

	echo
	echo "##### REMOTEPROC #####"
	run "for r in /sys/class/remoteproc/remoteproc*; do echo \"\$(cat \$r/name): \$(cat \$r/state)\"; done"

	echo
	echo "##### POWER #####"
	run "cat /sys/class/power_supply/bms/uevent 2>/dev/null; echo ---; cat /sys/class/power_supply/battery/uevent 2>/dev/null | head -12"

	echo
	echo "##### NET #####"
	run "ip -br addr; echo ---; iw dev wlan0 link 2>/dev/null | head -6"

	echo
	echo "##### STORAGE #####"
	run "lsblk 2>/dev/null | head -20; echo ---; cat /sys/block/sda/device/model 2>/dev/null"

	echo
	echo "##### THERMAL #####"
	run "for z in /sys/class/thermal/thermal_zone*; do echo \"\$(cat \$z/type): \$(cat \$z/temp)\"; done"

	echo
	echo "##### AUDIO #####"
	run "cat /proc/asound/cards; echo ---; sudo pactl list short sinks 2>/dev/null"

	echo
	echo "##### SENSORS #####"
	run "systemctl is-active hexagonrpcd-adsp-rootpd hexagonrpcd-adsp-sensorspd rain-sensor-bridge; busctl --system get-property net.hadess.SensorProxy /net/hadess/SensorProxy net.hadess.SensorProxy LightLevel 2>/dev/null"

	echo
	echo "##### I2C #####"
	run "for d in /sys/bus/i2c/devices/*; do drv=\$(basename \$(readlink -f \$d/driver 2>/dev/null) 2>/dev/null); [ -n \"\$drv\" ] && [ \"\$drv\" != driver ] && echo \"\$(basename \$d): \$drv\"; done"

	echo
	echo "##### INPUT #####"
	run "for i in /sys/class/input/input*; do cat \$i/name 2>/dev/null; done"

	echo
	echo "##### DMESG_ERR_WARN_COUNT #####"
	run "dmesg | grep -icE 'error|fail|warn'; echo ---; dmesg | grep -iE 'error|fail|warn' | tail -15"
} > "$OUT" 2>&1

$ADB shell "dmesg" > "$DMESG_OUT" 2>/dev/null
echo "baseline: $OUT ($(wc -l < "$OUT") lines)"
echo "dmesg:    $DMESG_OUT ($(wc -l < "$DMESG_OUT") lines)"
