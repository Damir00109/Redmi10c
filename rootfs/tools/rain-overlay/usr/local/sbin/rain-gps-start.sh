#!/bin/sh
# rain-gps-start.sh — enable ModemManager's Location interface.
#
# The GNSS engine lives in the modem and is reached through QMI LOC/PDS
# (QRTR service 16).  ModemManager exposes it as the Location interface, but
# it has to be enabled explicitly; gnss-share then republishes the NMEA on
# /var/run/gnss-share.sock for geoclue/apps.
#
# Works with no SIM inserted: standalone GNSS needs no subscription.
# A real position fix still needs a sky view (indoors only SV info arrives).
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

log() { echo "[rain-gps] $*" >&2; }

# Wait for ModemManager to export the modem
for i in $(seq 1 60); do
    mmcli -L 2>/dev/null | grep -q Modem && break
    sleep 2
done
mmcli -L 2>/dev/null | grep -q Modem || { log "no modem in ModemManager"; exit 0; }

mmcli -m 0 --location-enable-gps-nmea >/dev/null 2>&1 && log "gps-nmea enabled"
mmcli -m 0 --location-enable-gps-raw  >/dev/null 2>&1 && log "gps-raw enabled"

mmcli -m 0 --location-status 2>/dev/null | grep -E "capabilities|enabled" | sed 's/^/  /' >&2
exit 0
