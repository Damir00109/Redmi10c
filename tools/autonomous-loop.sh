#!/usr/bin/env bash
# Wait for phone (adb/fastboot/ACM) and act without user prompts.
set -u
ROOT=/home/damir00109/Desktop/Redmi10c
IMG=$ROOT/out/boot-usbtry3.img
DTBO=$ROOT/backup/ab-20260726-023543/active/dtbo.img
BOOT=$ROOT/backup/ab-20260726-023543/active/boot.img
LOG=$ROOT/logs/autonomous.log
STATE=$ROOT/logs/autonomous.state
exec >>"$LOG" 2>&1
echo "=== autonomous $(date -Is) pid=$$ ==="
echo idle >"$STATE"

have_adb() { adb devices 2>/dev/null | grep -q $'\tdevice$'; }
have_fb() { fastboot devices 2>/dev/null | grep -q .; }
have_acm() { ls /dev/ttyACM* >/dev/null 2>&1; }

flash_mainline() {
  echo "flash_mainline $(date -Is) img=$(ls -lh "$IMG" | awk '{print $5,$9}')"
  echo flashing >"$STATE"
  fastboot flash boot "$IMG" || return 1
  fastboot erase dtbo || true
  fastboot reboot || true
  echo waiting_acm >"$STATE"
  return 0
}

restore_android() {
  echo "restore_android $(date -Is)"
  echo restoring >"$STATE"
  fastboot flash dtbo "$DTBO" || true
  fastboot flash boot "$BOOT" || true
  fastboot reboot || true
  echo idle >"$STATE"
}

cycle_usb() {
  # only touch actual Android/QCOM devices — never blind uhubctl -a cycle
  for d in /sys/bus/usb/devices/*; do
    [ -f "$d/idVendor" ] || continue
    id=$(cat "$d/idVendor" 2>/dev/null)$(cat "$d/idProduct" 2>/dev/null)
    case "$id" in
      18d1*|2717*|22b8*|05c6*|0e8d*|2a45*) ;;
      *) continue ;;
    esac
    if [ -f "$d/authorized" ]; then
      echo 0 >"$d/authorized" 2>/dev/null || true
      sleep 1
      echo 1 >"$d/authorized" 2>/dev/null || true
    fi
  done
}

LAST_FLASH=0
while true; do
  if have_acm; then
    ACM=$(ls /dev/ttyACM* | head -1)
    echo "ACM $ACM $(date -Is)"
    echo acm >"$STATE"
    stty -F "$ACM" 115200 raw -echo 2>/dev/null || true
    printf '\nid\nuname -a\ncat /proc/version\nls /sys/class/udc\n' >"$ACM" 2>/dev/null || true
    timeout 8 cat "$ACM" 2>/dev/null || true
    sleep 20
    continue
  fi

  if have_adb; then
    NOW=$(date +%s)
    # avoid flash storm: at most once per 90s from adb
    if [ $((NOW - LAST_FLASH)) -lt 90 ]; then
      echo "ADB seen, cooldown $((90 - (NOW - LAST_FLASH)))s"
      sleep 5
      continue
    fi
    echo "ADB $(date -Is) -> bootloader"
    adb reboot bootloader || true
    for i in $(seq 1 45); do have_fb && break; sleep 1; done
    if have_fb; then
      flash_mainline && LAST_FLASH=$(date +%s)
      # wait for ACM up to 60s, else leave loop to restore next time if needed
      for i in $(seq 1 60); do
        have_acm && break
        have_adb && break
        have_fb && break
        sleep 1
      done
    fi
    continue
  fi

  if have_fb; then
    NOW=$(date +%s)
    if [ $((NOW - LAST_FLASH)) -lt 60 ]; then
      sleep 3
      continue
    fi
    echo "FASTBOOT $(date -Is)"
    flash_mainline && LAST_FLASH=$(date +%s)
    for i in $(seq 1 60); do
      have_acm && break
      have_adb && break
      have_fb && break
      sleep 1
    done
    continue
  fi

  echo "waiting_device $(date -Is)"
  echo waiting >"$STATE"
  cycle_usb
  sleep 5
done
