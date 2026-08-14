#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Flash ONE boot image once. No re-flash loops.
#
# Bug this fixes: after `fastboot reboot`, the phone can still show up in
# `fastboot devices` for a few seconds. Old waiters treated that as BACK_FB,
# restored/reflashed immediately → loop. Second manual fastboot then "worked"
# because the known-good image finally stuck.
set -euo pipefail

ROOT=/home/damir00109/Desktop/Redmi10c
LOG=$ROOT/logs/flash-usbtry.log
IMG=${IMG:-$ROOT/out/boot-usbtry3.img}
RESTORE_IMG=${RESTORE_IMG:-$ROOT/out/boot-usbtry3-thin-ok.img}
WAIT_FB=0
RESTORE_ON_FAIL=0
ACM_TIMEOUT=50
# ignore fastboot this many seconds after reboot command
GRACE=12

usage() {
  cat <<EOF
Usage: $0 [--wait] [--restore-on-fail] [--img PATH] [--grace SEC]
  --wait            wait until a fastboot device appears
  --restore-on-fail if boot falls back to fastboot after grace, flash RESTORE_IMG once
  --img PATH        boot image (default: out/boot-usbtry3.img)
  --grace SEC       seconds to ignore fastboot after reboot (default: $GRACE)
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --wait) WAIT_FB=1 ;;
    --restore-on-fail) RESTORE_ON_FAIL=1 ;;
    --img) IMG=$2; shift ;;
    --grace) GRACE=$2; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "unknown arg: $1" >&2; usage; exit 2 ;;
  esac
  shift
done

have_fb() { fastboot devices 2>/dev/null | grep -q .; }
have_acm() { ls /dev/ttyACM* >/dev/null 2>&1; }
have_gadget() { lsusb 2>/dev/null | grep -q '1d6b:0104'; }

log() { echo "$*" | tee -a "$LOG"; }

[ -f "$IMG" ] || { echo "missing img: $IMG" >&2; exit 1; }

# stop other flash waiters / autonomous loops that cause storms
pkill -f "$ROOT/tools/autonomous-loop.sh" 2>/dev/null || true
pkill -f "$ROOT/tools/wait-flash-quiet.sh" 2>/dev/null || true
pkill -f "$ROOT/tools/flash-usbtry-now.sh" 2>/dev/null || true
# don't kill ourselves

if [ "$WAIT_FB" -eq 1 ]; then
  log "flash-once wait_fb $(date -Is) img=$(basename "$IMG")"
  while ! have_fb; do sleep 1; done
else
  have_fb || { echo "no fastboot device (pass --wait)" >&2; exit 1; }
fi

log "flash-once got_fb $(date -Is) img=$(basename "$IMG")"
fastboot flash boot "$IMG"
fastboot erase dtbo
fastboot reboot
log "flash-once rebooted $(date -Is)"

# 1) wait until phone LEAVES fastboot (critical — avoids false BACK_FB)
left=0
for i in $(seq 1 20); do
  if ! have_fb; then left=1; break; fi
  sleep 1
done
if [ "$left" -ne 1 ]; then
  log "flash-once WARN still in fastboot after reboot cmd"
fi

# 2) grace: boot enumeration / crash-to-FB race window
sleep "$GRACE"

# 3) poll ACM; only then treat FB as real failure
for i in $(seq 1 "$ACM_TIMEOUT"); do
  if have_acm || have_gadget; then
    log "flash-once ACM_OK $(date -Is) $(ls /dev/ttyACM* 2>/dev/null | tr '\n' ' ')"
    if [ -x "$ROOT/tools/acm" ] && have_acm; then
      "$ROOT/tools/acm" -w 4 'busybox echo FLASH_OK; busybox cat /proc/version; busybox dmesg | busybox grep -iE "gcc|ufs|ufshcd|ahb2phy|clock-controller|rain:" | busybox head -40' \
        | tee -a "$ROOT/logs/acm-verify.txt" || true
      sleep 10
      "$ROOT/tools/acm" -w 3 'busybox echo ACM_STILL' \
        | tee -a "$ROOT/logs/acm-verify.txt" || true
    fi
    exit 0
  fi
  if have_fb; then
    log "flash-once BACK_FB after grace $(date -Is) t=${i}s"
    if [ "$RESTORE_ON_FAIL" -eq 1 ] && [ -f "$RESTORE_IMG" ]; then
      log "flash-once restore $(basename "$RESTORE_IMG")"
      fastboot flash boot "$RESTORE_IMG"
      fastboot erase dtbo
      fastboot reboot
      # leave FB, grace, wait ACM — no further restore loop
      for j in $(seq 1 20); do have_fb || break; sleep 1; done
      sleep "$GRACE"
      for j in $(seq 1 40); do
        if have_acm || have_gadget; then
          log "flash-once RESTORE_ACM_OK $(date -Is)"
          exit 2
        fi
        sleep 1
      done
      log "flash-once restore_no_acm"
      exit 3
    fi
    exit 4
  fi
  sleep 1
done

log "flash-once NO_ACM $(date -Is)"
exit 1
