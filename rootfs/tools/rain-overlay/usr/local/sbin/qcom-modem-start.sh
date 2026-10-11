#!/bin/bash
# Start the Qualcomm modem via remoteproc PAS.
# Requires: qrtr-ns, rmtfs, pd-mapper, tqftpserv already running.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

RP=/sys/class/remoteproc/remoteproc0
[ -d "$RP" ] || { echo "FATAL: no modem remoteproc" >&2; exit 1; }

# Already running? Idempotent.
st=$(cat "$RP/state" 2>/dev/null)
if [ "$st" = running ]; then
  echo "modem already running — skip"
  exit 0
fi

# Disable recovery — we handle crashes manually
[ -e "$RP/recovery" ] && echo disabled >"$RP/recovery"

# Stop if in a weird state
if [ "$st" != offline ] && [ -n "$st" ]; then
  echo stop >"$RP/state" 2>/dev/null || true
  for i in $(seq 1 10); do
    [ "$(cat "$RP/state")" = offline ] && break
    sleep 1
  done
fi

echo "starting modem..."
echo start >"$RP/state" || { echo "FATAL: modem start failed" >&2; exit 1; }

# Wait for modem to reach running state (with stability check)
ok=0
for i in $(seq 1 40); do
  st=$(cat "$RP/state")
  case "$st" in
    running) ok=$((ok+1)); [ $ok -ge 3 ] && break ;;
    crashed|offline) echo "FATAL: modem $st" >&2; exit 1 ;;
    *) ok=0 ;;
  esac
  sleep 1
done

[ "$(cat "$RP/state")" = running ] || { echo "FATAL: modem not stable" >&2; exit 1; }
echo "modem is up"
exit 0
