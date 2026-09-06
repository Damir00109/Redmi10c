#!/bin/bash
# Load Bluetooth hci_uart for WCN3990 and start PulseAudio user session.
# Wi-Fi/modem remoteproc is built into the kernel — no script needed for that.
set +e
export PATH=/usr/local/sbin:/usr/sbin:/usr/bin:/sbin:/bin

# Already up?
if hciconfig hci0 >/dev/null 2>&1 && hciconfig hci0 | grep -q "UP RUNNING"; then
  systemctl start bluetooth 2>/dev/null || true
  systemctl start user@1000 2>/dev/null || true
  exit 0
fi

modprobe hci_uart 2>/dev/null || true
for i in $(seq 1 10); do
  hciconfig hci0 >/dev/null 2>&1 && break
  sleep 1
done

if hciconfig hci0 >/dev/null 2>&1; then
  hciconfig hci0 up 2>/dev/null || true
  systemctl start bluetooth 2>/dev/null || true
  # Start PulseAudio user session for rain (requires linger enabled)
  systemctl start user@1000 2>/dev/null || true
  echo "BT: hci0 up"
else
  echo "BT: hci0 not found"
fi
exit 0
