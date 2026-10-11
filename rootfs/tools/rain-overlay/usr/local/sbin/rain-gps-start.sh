#!/bin/sh
# rain-gps-start.sh — bring up the GNSS stack.
#
# The GNSS engine lives in the modem and is reached through QMI LOC/PDS
# (QRTR service 16).  ModemManager exposes it as the Location interface;
# gnss-share republishes the NMEA on /var/run/gnss-share.sock for geoclue/apps.
#
# What this enables:
#   gps-nmea, gps-raw  — standalone GNSS (NMEA + raw position)
#   3gpp-lac-ci        — cell-based location (works with a SIM)
#   agps-msa/msb       — best effort: this modem firmware accepts the request
#                        but keeps the engine in "standalone" and never sends
#                        the operation-mode indication, so MM reports an error
#                        (harmless, logged here)
#
# Notes:
#   * The modem must be ENABLED first, otherwise MM refuses:
#     "Wrong state: Cannot enable location: device not yet enabled".
#   * A position fix needs a sky view; indoors only $GPGSV arrives.
#   * Do NOT touch `qmicli --loc-set-nmea-types` while MM owns the session:
#     it kills MM's NMEA stream until ModemManager is restarted.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

log() { echo "[rain-gps] $*" >&2; }

# Wait for ModemManager to export the modem
for i in $(seq 1 60); do
    mmcli -L 2>/dev/null | grep -q Modem && break
    sleep 2
done
mmcli -L 2>/dev/null | grep -q Modem || { log "no modem in ModemManager"; exit 0; }

# The Location interface can only be configured on an enabled modem.
state=$(mmcli -m 0 2>/dev/null | sed -n 's/.*state: *//p' | head -1)
case "$state" in
    disabled|"") mmcli -m 0 --enable >/dev/null 2>&1 && log "modem enabled (was: ${state:-unknown})" ;;
esac

for f in gps-nmea gps-raw 3gpp; do
    mmcli -m 0 --location-enable-"$f" >/dev/null 2>&1 && log "enabled $f"
done
for f in agps-msa agps-msb; do
    mmcli -m 0 --location-enable-"$f" >/dev/null 2>&1 \
        || log "$f refused by the modem firmware (standalone only)"
done

mmcli -m 0 --location-status 2>/dev/null | grep -E "capabilities|enabled" | sed 's/^/  /' >&2
exit 0
