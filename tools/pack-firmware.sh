#!/usr/bin/env bash
# pack-firmware.sh — pack firmware blobs + vendor files into the release asset.
#
# The tarball has two top-level dirs:
#   firmware/  — installed to /lib/firmware (initramfs + rootfs)
#   qcom/      — hexagonrpcd -R tree, installed to /usr/share/qcom in the rootfs
#
# Usage: tools/pack-firmware.sh [output.tar.zst]
# Then upload with:
#   gh release upload <tag> <file> --clobber -R Damir00109/Redmi10c
set -euo pipefail

R="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-/tmp/redmi10c-firmware.tar.zst}"

[ -d "$R/initramfs/lib/firmware" ] || {
	echo "no $R/initramfs/lib/firmware" >&2
	exit 1
}
[ -d "$R/vendor-qcom/qcom" ] || {
	echo "no $R/vendor-qcom/qcom (stage vendor files first)" >&2
	exit 1
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP"
cp -a "$R/initramfs/lib/firmware" "$TMP/firmware"
cp -a "$R/vendor-qcom/qcom" "$TMP/qcom"

# IPA (GSI/uC) firmware: ipa.ko requests ipa_fws.mdt while pivot-init loads it,
# before the rootfs exists — so it must ship in the initramfs /lib/firmware.
# Staged separately in firmware/ipa; make sure it lands in the tarball.
if [ -d "$R/firmware/ipa" ]; then
	cp "$R"/firmware/ipa/ipa_fws.* "$R"/firmware/ipa/scuba_ipa_fws.* "$TMP/firmware/"
fi
[ -f "$TMP/firmware/ipa_fws.mdt" ] || {
	echo "ipa_fws.* missing from $TMP/firmware" >&2
	exit 1
}

tar -C "$TMP" -cf - firmware qcom | zstd -12 -T0 -o "$OUT"
ls -la "$OUT"
echo ">> upload with: gh release upload firmware-2026.09.25 $OUT --clobber -R Damir00109/Redmi10c"
