#!/usr/bin/env bash
# Build iio-sensor-proxy with the Snapdragon Sensor Core backend.
#
# iio-sensor-proxy >= 3.9 can talk to the SSC through libssc, which is what we
# need: our sensors live behind the ADSP, so there are no kernel IIO devices and
# the distro package (3.8 on Ubuntu 26.04) has no SSC support at all.  With this
# build the desktop gets AccelerometerOrientation / LightLevel from the real
# service instead of our python bridge.
#
# Run tools/build-libssc.sh first (or this script will do it): the cross build
# needs a libssc.pc that points at the aarch64 library.
#
# Build dependencies (aarch64 multiarch):
#   libglib2.0-dev:arm64 libgudev-1.0-dev:arm64 libpolkit-gobject-1-dev:arm64
#   libudev-dev:arm64 libsystemd-dev:arm64
#
# Output: sensors/iio/{iio-sensor-proxy,monitor-sensor} + data files
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/cache"
SRC="$CACHE/iio-sensor-proxy"
BUILD="$SRC/build-aarch64"
STAGE="$CACHE/libssc-cross"
OUT="$ROOT/sensors/iio"

for pc in glib-2.0 gio-2.0 gudev-1.0 polkit-gobject-1 libudev; do
	if ! PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
		pkg-config --exists "$pc"; then
		echo "missing aarch64 pkg-config module: $pc" >&2
		exit 1
	fi
done

# libssc must be built first: iio-sensor-proxy links against it and the cross
# pkg-config has no idea about our sensors/lib-glibc directory.
if [ ! -f "$ROOT/sensors/lib-glibc/libssc.so.2" ]; then
	"$ROOT/tools/build-libssc.sh"
fi

if [ ! -f "$STAGE/lib/pkgconfig/libssc.pc" ]; then
	mkdir -p "$STAGE/lib/pkgconfig" "$STAGE/include"
	cp -a "$CACHE/libssc/src/libssc"/*.h "$STAGE/include/" 2>/dev/null || \
		cp -a "$CACHE/libssc/build-aarch64"/src/*.h "$STAGE/include/" 2>/dev/null || true
	mkdir -p "$STAGE/include/libssc"
	cp -a "$STAGE"/*.h "$STAGE/include/libssc/" 2>/dev/null || true
	cp -a "$ROOT"/sensors/lib-glibc/libssc.so* "$STAGE/lib/"
	cat > "$STAGE/lib/pkgconfig/libssc.pc" <<-EOF
	prefix=$STAGE
	includedir=\${prefix}/include
	libdir=\${prefix}/lib

	Name: libssc
	Description: Library to access sensors of Qualcomm Sensor Core
	Version: 0.4.4
	Requires: glib-2.0, gobject-2.0, gio-2.0, qmi-glib
	Libs: -L\${libdir} -lssc
	Cflags: -I\${includedir}/libssc
	EOF
fi

if [ ! -d "$SRC/.git" ]; then
	mkdir -p "$CACHE"
	git clone --depth 1 https://gitlab.freedesktop.org/hadess/iio-sensor-proxy "$SRC"
fi

if [ ! -f "$SRC/cross-aarch64.ini" ]; then
	cat > "$SRC/cross-aarch64.ini" <<-EOF
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
	pkg_config_libdir = '$STAGE/lib/pkgconfig:/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig'
	EOF
fi

cd "$SRC"
if [ ! -d "$BUILD" ]; then
	meson setup "$BUILD" --cross-file cross-aarch64.ini \
		-Dssc-support=enabled \
		-Dudevrulesdir=/usr/lib/udev/rules.d \
		-Dsystemdsystemunitdir=/usr/lib/systemd/system \
		--prefix=/usr
fi
ninja -C "$BUILD"

rm -rf "$OUT"
DESTDIR="$OUT" ninja -C "$BUILD" install >/dev/null

echo ">> iio-sensor-proxy $(sed -n "s/.*version: '\([0-9.]*\)'.*/\1/p" "$SRC/meson.build" | head -n1) staged in sensors/iio"
