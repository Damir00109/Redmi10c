#!/usr/bin/env bash
# pack-firmware.sh — pack the current initramfs firmware into a release asset.
#
# Usage: tools/pack-firmware.sh [output.tar.zst]
# Then upload with:
#   gh release create <tag> -R owner/repo --target main <output.tar.zst>
set -euo pipefail

R="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-/tmp/redmi10c-firmware.tar.zst}"

[ -d "$R/initramfs/lib/firmware" ] || {
	echo "no $R/initramfs/lib/firmware" >&2
	exit 1
}

tar -C "$R/initramfs/lib" -cf - firmware | zstd -12 -T0 -o "$OUT"
ls -la "$OUT"
echo ">> upload with: gh release create <tag> -R Damir00109/Redmi10c --target main $OUT"
