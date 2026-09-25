#!/usr/bin/env bash
# boot-and-check.sh [img] — boot rescue image and dump GPU/SMMU/driver state.
# Iteration helper for GPU bring-up: no flashing, temporary fastboot boot only.
set -uo pipefail
R=/home/damir00109/Desktop/Redmi10C_UPDATE/Kernel_Redmi10c
IMG=${1:-$R/out/boot-display-console-rescue.img}
DEV=/dev/ttyACM0

log() { printf '%s\n' "$*"; }

# 1) get to fastboot (from rescue console, Android, or already-fastboot)
if [ -e "$DEV" ] && ! timeout 5 adb devices 2>/dev/null | grep -qw device; then
    log "in rescue -> reboot to bootloader via console"
    printf 'reboot -f\r' > "$DEV" 2>/dev/null
fi
# wait for either adb (Android came up) or fastboot
for i in $(seq 1 40); do
    timeout 5 fastboot devices 2>/dev/null | grep -qw fastboot && break
    if timeout 5 adb devices 2>/dev/null | grep -qw device; then
        log "android up -> adb reboot bootloader"
        adb reboot bootloader >/dev/null 2>&1
        break
    fi
    sleep 4
done
for i in $(seq 1 25); do
    timeout 5 fastboot devices 2>/dev/null | grep -qw fastboot && break
    sleep 3
done
timeout 20 fastboot set_active b >/dev/null 2>&1
timeout 20 fastboot reboot bootloader >/dev/null 2>&1; sleep 12
timeout 5 fastboot devices 2>/dev/null | grep -qw fastboot || { log "NO FASTBOOT"; exit 1; }

# 2) boot
timeout 180 fastboot boot "$IMG" 2>&1 | tail -2

# 3) wait for ACM
for i in $(seq 1 40); do
    [ -e "$DEV" ] && break
    sleep 4
done
[ -e "$DEV" ] || { log "NO ACM"; exit 1; }

# 4) console plumbing
fuser -k "$DEV" >/dev/null 2>&1; sleep 1
stty -F "$DEV" 115200 raw -echo 2>/dev/null
: > /tmp/ser.log
setsid bash -c "exec cat $DEV >> /tmp/ser.log 2>&1" </dev/null >/dev/null 2>&1 &
sleep 1
printf '\003' > "$DEV"; sleep 1; printf '\r' > "$DEV"; sleep 1

paced() {
    local s="$1"
    while [ -n "$s" ]; do
        printf '%s' "${s:0:32}" > "$DEV"; s="${s:32}"; sleep 0.35
    done
    printf '\r' > "$DEV"; sleep 0.8
}

# 5) checks
paced "uname -r; ls /dev/dri 2>&1"
paced "ls /dev/dri 2>&1; echo ---; ls /sys/bus/platform/devices/ | grep -E '5900000|596a000|5990000|59a0000'"
paced "ls -l /sys/bus/platform/devices/5990000.clock-controller/driver 2>&1 | tail -1"
paced "dmesg | grep -iE 'adreno|smmu|msm_gpu|msm_dpu|msm_drm|a6xx|gpucc|zap|gmu|gpu|drm' | tail -30"
sleep 5
log "===== CONSOLE TAIL ====="
tail -45 /tmp/ser.log
