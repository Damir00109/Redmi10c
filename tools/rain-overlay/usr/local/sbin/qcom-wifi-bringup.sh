#!/bin/bash
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/qcom-wifi-bringup.log

{
  echo "=== $(date -Is) automatic Wi-Fi bringup ==="
  /usr/local/sbin/qcom-wifi-start.sh
  WIFI_ASSOC_TIMEOUT=45 /usr/local/sbin/qcom-wifi-connect.sh
  echo "=== automatic Wi-Fi bringup complete ==="
} >>"$LOG" 2>&1
