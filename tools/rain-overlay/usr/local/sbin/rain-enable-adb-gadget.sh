#!/bin/bash
# Switch rain USB from ACM serial to ADB gadget (oneshot install helper).
set -euo pipefail
export PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

echo "Installing ADB gadget units and disabling ACM console..."

systemctl daemon-reload

systemctl disable --now rain-serial-console.service 2>/dev/null || true
systemctl disable --now usb-acm-gadget.service 2>/dev/null || true

systemctl enable usb-adb-gadget.service
systemctl enable adbd.service

echo "Enabled usb-adb-gadget + adbd; ACM/serial disabled."
echo "Reboot recommended for clean UDC rebind on SM6225."
echo "After reboot: host should see 'adb devices' (18d1:4ee7)."
