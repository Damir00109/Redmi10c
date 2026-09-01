#!/bin/bash
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
systemctl stop NetworkManager.service 2>/dev/null || true
/usr/local/sbin/qcom-wifi-start.sh
systemctl unmask NetworkManager.service wpa_supplicant.service
systemctl start wpa_supplicant.service
systemctl start NetworkManager.service
