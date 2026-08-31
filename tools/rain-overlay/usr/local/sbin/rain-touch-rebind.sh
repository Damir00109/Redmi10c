#!/bin/sh
# Rebind fts_ts touch driver after rootfs is fully mounted.
# During early boot, request_firmware() fails for focaltech_ts_fw_xinli.bin
# because the firmware path isn't available yet. This script unbinds and
# rebinds the SPI touch driver so it can load firmware successfully.
set +e
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

# Wait for firmware file to be accessible
for i in 1 2 3 4 5 6 7 8 9 10; do
  [ -f /lib/firmware/focaltech_ts_fw_xinli.bin ] && break
  sleep 1
done

# Rebind touch driver
if [ -e /sys/bus/spi/drivers/fts_ts/bind ]; then
  echo spi0.0 > /sys/bus/spi/drivers/fts_ts/unbind 2>/dev/null
  sleep 1
  echo spi0.0 > /sys/bus/spi/drivers/fts_ts/bind 2>/dev/null
  sleep 3
fi

# Verify touch events
if [ -e /dev/input/event4 ]; then
  echo "fts_ts rebound — touch should work"
else
  echo "WARNING: /dev/input/event4 not found after rebind"
fi
