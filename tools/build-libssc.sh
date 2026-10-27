#!/usr/bin/env bash
# Build libssc + ssccli from the open-source libssc project.
#
# This is the client side of the Snapdragon Sensor Core stack: ssccli talks to
# the sensor PD (served by hexagonrpcd) over QMI and libssc is the GLib library
# other projects (iio-sensor-proxy) link against.
#
# Building it ourselves gives a glibc binary, so the musl runtime that only
# existed for the postmarketOS builds can be dropped once both hexagonrpcd and
# ssccli are built here.
#
# Build dependencies (aarch64 multiarch):
#   libglib2.0-dev:arm64 libqmi-glib-dev:arm64 libprotobuf-c-dev:arm64
#
# Output: sensors/bin/ssccli and sensors/lib-glibc/libssc.so.*
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/cache"
SRC="$CACHE/libssc"
BUILD="$SRC/build-aarch64"

for pc in glib-2.0 gio-unix-2.0 qmi-glib libprotobuf-c; do
	if ! PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
		pkg-config --exists "$pc"; then
		echo "missing aarch64 pkg-config module: $pc" >&2
		echo "install libglib2.0-dev:arm64 libqmi-glib-dev:arm64 libprotobuf-c-dev:arm64" >&2
		exit 1
	fi
done

if [ ! -d "$SRC/.git" ]; then
	mkdir -p "$CACHE"
	git clone --depth 1 https://codeberg.org/DylanVanAssche/libssc "$SRC"
fi

if [ ! -f "$SRC/cross-aarch64.ini" ]; then
	cat > "$SRC/cross-aarch64.ini" <<-'EOF'
	[binaries]
	c = 'aarch64-linux-gnu-gcc'
	ar = 'aarch64-linux-gnu-ar'
	strip = 'aarch64-linux-gnu-strip'
	pkgconfig = 'pkg-config'

	[host_machine]
	system = 'linux'
	cpu_family = 'aarch64'
	cpu = 'aarch64'
	endian = 'little'

	[properties]
	pkg_config_libdir = '/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig'
	EOF
fi

cd "$SRC"
if [ ! -d "$BUILD" ]; then
	meson setup "$BUILD" --cross-file cross-aarch64.ini
fi
ninja -C "$BUILD"

install -d "$ROOT/sensors/bin" "$ROOT/sensors/lib-glibc"
install -m755 "$BUILD/src/ssccli" "$ROOT/sensors/bin/ssccli"
for f in "$BUILD"/src/libssc.so.*; do
	[ -f "$f" ] && install -m755 "$f" "$ROOT/sensors/lib-glibc/"
done

# hexagonrpcd and ssccli are now both glibc builds, so the musl runtime and the
# unused monitor-sensor binary that come with the postmarketOS payload are not
# needed any more.
if [ -x "$ROOT/sensors/bin/hexagonrpcd" ]; then
	rm -rf "$ROOT/sensors/lib-musl" "$ROOT/sensors/ld-musl-aarch64.so.1"
	rm -f "$ROOT/sensors/bin/monitor-sensor"
fi

echo ">> ssccli $(sed -n "s/.*version: '\([0-9.]*\)'.*/\1/p" "$SRC/meson.build" | head -n1) staged"
