#!/bin/sh
# setandroid.sh — switch active boot slot to A (Android) from Linux.
#
# Qualcomm A/B devices store the active slot in the devinfo partition
# (/dev/sde45 on Redmi 10C). The slot byte is at offset 0x90:
#   0x00 = slot A (Android)
#   0x01 = slot B (Linux/Ubuntu)
#
# Usage: sudo setandroid.sh
# After running, reboot — XBL/abl will boot from slot A.

set -e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

DEVINFO=/dev/sde45
SLOT_OFFSET=144  # 0x90

# Find devinfo partition by name (fallback)
if [ ! -b "$DEVINFO" ]; then
    for p in /sys/block/sde/sde*; do
        pn=$(cat "$p/uevent" 2>/dev/null | grep PARTNAME | cut -d= -f2)
        if [ "$pn" = "devinfo" ]; then
            DEVINFO="/dev/$(basename "$p")"
            break
        fi
    done
fi

if [ ! -b "$DEVINFO" ]; then
    echo "ERROR: devinfo partition not found"
    exit 1
fi

# Verify magic (13 bytes = "ANDROID-BOOT!")
magic=$(dd if="$DEVINFO" bs=1 count=13 2>/dev/null | od -An -tx1 | tr -d ' \n')
if [ "$magic" != "414e44524f49442d424f4f5421" ]; then
    echo "ERROR: devinfo magic mismatch (expected ANDROID-BOOT!)"
    echo "Got: $magic"
    exit 1
fi

# Read current slot
current=$(dd if="$DEVINFO" bs=1 count=1 skip=$SLOT_OFFSET 2>/dev/null | od -An -tu1 | tr -d ' ')
case "$current" in
    0) echo "Current slot: A (Android) — already set"; exit 0 ;;
    1) echo "Current slot: B (Linux) — switching to A (Android)" ;;
    *) echo "ERROR: unexpected slot value: $current"; exit 1 ;;
esac

# Write slot A (0x00)
printf '\000' | dd of="$DEVINFO" bs=1 seek=$SLOT_OFFSET count=1 conv=notrunc 2>/dev/null
sync

# Verify
new=$(dd if="$DEVINFO" bs=1 count=1 skip=$SLOT_OFFSET 2>/dev/null | od -An -tu1 | tr -d ' ')
if [ "$new" = "0" ]; then
    echo "OK: active slot set to A (Android)"
    echo "Reboot to boot into Android: sudo reboot"
else
    echo "ERROR: verification failed, slot value = $new"
    exit 1
fi
