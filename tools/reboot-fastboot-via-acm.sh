#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# From mainline ACM shell: write Android BCB to misc, reboot into fastboot.
# Usage: tools/reboot-fastboot-via-acm.sh [/dev/ttyACM0]
set -euo pipefail
ACM="${1:-${ACM:-/dev/ttyACM0}}"
python3 -u - "$ACM" <<'PY'
import serial, sys, time
acm = sys.argv[1]
ser = serial.Serial(acm, 115200, timeout=1)

def drain(t=0.6):
    time.sleep(t)
    bits = []
    while True:
        d = ser.read(65536)
        if not d:
            break
        bits.append(d)
    return b"".join(bits).decode(errors="replace")

def cmd(c, wait=1.0):
    ser.write((c + "\n").encode())
    out = drain(wait)
    sys.stdout.write(out)
    sys.stdout.flush()
    return out

for _ in range(5):
    ser.write(b"\x03\n")
    time.sleep(0.15)
    ser.read(100000)

cmds = [
    "MISC=",
    'for u in /sys/block/*/*/uevent; do pn=$(busybox grep "^PARTNAME=" "$u" 2>/dev/null); pn=${pn#PARTNAME=}; [ "$pn" = misc ] || continue; MISC=/dev/$(busybox basename "$(busybox dirname "$u")"); break; done',
    'echo MISC=$MISC',
    '[ -b "$MISC" ] || { echo NO_MISC; exit 1; }',
    "busybox dd if=/dev/zero of=/tmp/bcb bs=2048 count=1 2>/dev/null",
    "busybox printf bootonce-bootloader | busybox dd of=/tmp/bcb bs=1 seek=0 conv=notrunc 2>/dev/null",
    'busybox dd if=/tmp/bcb of="$MISC" bs=2048 count=1 2>/dev/null',
    "busybox sync",
    "echo BCB_WRITTEN",
    "busybox reboot -f",
]
for c in cmds:
    cmd(c, wait=0.8 if "reboot" not in c else 0.3)
ser.close()
print("\n[host] waiting for fastboot...", flush=True)
PY

for i in $(seq 1 45); do
  if fastboot devices 2>/dev/null | grep -q .; then
    echo "FASTBOOT_OK after ${i}s"
    fastboot devices
    exit 0
  fi
  # also accept if ACM disappeared then fastboot appears
  sleep 1
done
echo "FASTBOOT_TIMEOUT (ABL may ignore BCB on this build)" >&2
exit 1
