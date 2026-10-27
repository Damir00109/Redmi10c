#!/usr/bin/env bash
# Build hexagonrpcd from the open-source linux-msm/hexagonrpc project.
#
# The daemon that serves the Snapdragon Sensor Core filesystem to the ADSP is
# free software (GPL-3.0), so we build it ourselves instead of shipping the
# musl binary from the postmarketOS image:
#   - it is a glibc build, so the musl runtime is only needed for ssccli
#   - version 0.5.0 builds the /mnt/vendor/persist and /sys/devices/soc0 trees
#     itself, which lets sensors-setup drop its staging hacks
#
# Output: sensors/bin/hexagonrpcd and sensors/lib-glibc/libhexagonrpc.so.*
# (both live under the gitignored sensors/ tree, like the other blobs).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/cache"
SRC="$CACHE/hexagonrpc"
REF="${HEXAGONRPC_REF:-main}"
BUILD="$SRC/build-aarch64"
UAPI="$ROOT/out/src/linux/include/uapi"

if [ ! -f "$UAPI/misc/fastrpc.h" ]; then
	echo "missing $UAPI/misc/fastrpc.h — build the kernel tree first (./build.sh)" >&2
	exit 1
fi

if [ ! -d "$SRC/.git" ]; then
	mkdir -p "$CACHE"
	git clone --depth 1 --branch "$REF" \
		https://github.com/linux-msm/hexagonrpc "$SRC"
fi

if [ ! -f "$SRC/cross-aarch64.ini" ]; then
	cat > "$SRC/cross-aarch64.ini" <<-'EOF'
	[binaries]
	c = 'aarch64-linux-gnu-gcc'
	ar = 'aarch64-linux-gnu-ar'
	strip = 'aarch64-linux-gnu-strip'

	[host_machine]
	system = 'linux'
	cpu_family = 'aarch64'
	cpu = 'aarch64'
	endian = 'little'
	EOF
fi

cd "$SRC"
if [ ! -d "$BUILD" ]; then
	meson setup "$BUILD" --cross-file cross-aarch64.ini \
		-Dc_args="-I$UAPI"
fi
ninja -C "$BUILD"

install -d "$ROOT/sensors/bin" "$ROOT/sensors/lib-glibc"
install -m755 "$BUILD/hexagonrpcd/hexagonrpcd" "$ROOT/sensors/bin/hexagonrpcd"
for f in "$BUILD"/libhexagonrpc/libhexagonrpc.so.*; do
	[ -f "$f" ] && install -m755 "$f" "$ROOT/sensors/lib-glibc/"
done

VER=$(sed -n "s/.*version: '\([0-9.]*\)'.*/\1/p" "$SRC/meson.build" | head -n1)
echo ">> hexagonrpcd $VER staged"
