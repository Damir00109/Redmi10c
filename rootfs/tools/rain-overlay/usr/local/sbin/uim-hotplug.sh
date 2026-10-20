#!/bin/sh
# uim-hotplug.sh — keep the SIM provisioned across runtime insert/remove.
#
# The modem reports card/tray changes as UIM slot-status indications; this
# daemon watches them and re-runs the provisioning (uim-provision.sh), then
# nudges ModemManager to re-probe.  Without it a hot-swapped SIM stays
# unprovisioned until the next boot.
QRTR_NODE=${QRTR_NODE:-0}
QRTR=qrtr://$QRTR_NODE
log() { echo "[uim-hotplug] $*" >&2; }

while :; do
    # Streams until the modem restarts (or qmicli dies) — then we reconnect.
    qmicli -d "$QRTR" --uim-monitor-slot-status 2>/dev/null | while read -r line; do
        case "$line" in
            *"Received slot status indication"*)
                log "slot status changed"
                sleep 3
                /usr/local/sbin/uim-provision.sh
                systemctl --no-block try-restart ModemManager.service
                ;;
        esac
    done
    sleep 5
done
