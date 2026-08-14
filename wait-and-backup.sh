#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Wait for phone over adb, then run full partition dump.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT="$ROOT/backup/backup-$(date +%Y%m%d-%H%M%S)"

echo "=== Redmi 10C backup waiter ==="
echo "On phone:"
echo "  1) USB debugging ON"
echo "  2) File transfer / MTP mode (not charge-only)"
echo "  3) Magisk/Lineage: Rooted debugging / allow root for ADB"
echo "  4) Unlock screen, accept RSA prompt if shown"
echo
echo "Waiting for adb device..."

adb start-server >/dev/null
adb wait-for-device

echo "Device seen:"
adb devices -l
echo

timeout 3 adb root >/dev/null 2>&1 || true
adb wait-for-device

if adb shell su -c id 2>/dev/null | grep -q uid=0; then
  echo "Root OK (Magisk su)"
else
  echo "WARN: Magisk su not ready. Grant root to shell, then re-run."
  exit 1
fi

exec "$ROOT/backup-partitions.sh" "$OUT"
