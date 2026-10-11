#!/bin/sh
# uim-provision.sh — prepare the SIM for ModemManager.
#
# Only the SIM in PHYSICAL SLOT 1 is supported.
#
# Why slot 2 is unsupported (measured 2026-10-11, see AGENTS.md):
#   * registration / voice / SMS work in slot 2, but every data bearer is
#     refused by the modem with "cm error: no-service";
#   * the refusal survives every userspace permutation — session bound as
#     primary or as secondary, slot mapping physical2->logical1 (primary) or
#     physical2->logical2 (default), fresh modem, healthy ModemManager,
#     IPv4/IPv4-IPv6, mcfg.tmp present/empty/absent, PDC config active or not;
#   * Android works in both slots.  Its RIL binds the SIM in slot 2 as a
#     SECONDARY GW provisioning session and keeps its own data subscription
#     (activeDataSubId = the SIM's slot) in modem NV.  libqmi/ModemManager
#     have no way to express either (no DSD bind-subscription operation, no
#     per-subscription QMI client, and qmicli 1.38 segfaults on
#     --pdc-load-config), so the modem keeps answering "no-service".
#
# Therefore physical slot 2 is powered off at boot and never provisioned.
QRTR=qrtr://0
log() { echo "[uim-provision] $*" >&2; }

card_status() { qmicli -d "$QRTR" --uim-get-card-status 2>/dev/null; }
slot_status() { qmicli -d "$QRTR" --uim-get-slot-status 2>/dev/null; }

# --- physical slot 2: unsupported, keep it uninitialised ---
for i in $(seq 1 60); do
    SLOTS=$(slot_status) && [ -n "$SLOTS" ] && break
    sleep 2
done
if [ -n "$SLOTS" ]; then
    if echo "$SLOTS" | awk '/^  Physical slot 2:/{p=1} p&&/Card status: present/{print; exit}' | grep -q present; then
        log "SIM in physical slot 2 — unsupported (data needs the modem DSD subscription, set only by Android's RIL)"
    fi
    qmicli -d "$QRTR" --uim-sim-power-off=2 >/dev/null 2>&1 \
        && log "physical slot 2 powered off"
    # Nothing in any slot -> nothing to provision; exit before the unit's timeout.
    if ! echo "$SLOTS" | grep -q "Card status: present"; then
        log "no SIM inserted"
        exit 0
    fi
fi

# --- wait for the USIM app (modem powers the card well after UIM is up) ---
STATUS=""
for i in $(seq 1 90); do
    STATUS=$(card_status)
    echo "$STATUS" | grep -q "Application type:.*usim" && break
    sleep 2
done
echo "$STATUS" | grep -q "Application type:.*usim" || { log "no SIM app detected"; exit 0; }

# Already provisioned (survives modem restarts in most cases) — nothing to do.
echo "$STATUS" | grep -q "Primary GW:   session doesn't exist" || { log "already provisioned"; exit 0; }

# Find the first USIM app in ANY slot: track the current "Slot [N]" header,
# then take the AID printed on the line after "Application ID:".
PAIR=$(echo "$STATUS" | awk '
    /^Slot \[/ { slot=$2; gsub(/[^0-9]/, "", slot); usim=0 }
    /Application type:.*usim/ { usim=1 }
    /Application ID:/ && usim && !done {
        getline; while ($0 ~ /^[ \t]*$/) getline
        gsub(/[ \t]/, "")
        printf "%s %s", slot, $0; done=1
    }')
SLOT=${PAIR%% *}; AID=${PAIR##* }
[ -n "$SLOT" ] && [ -n "$AID" ] && [ "$SLOT" != "$AID" ] || { log "no SIM app detected"; exit 0; }

if [ "$SLOT" != "1" ]; then
    log "SIM reports on logical slot $SLOT (not 1) — run 'mmcli -m 0 --set-primary-sim-slot=1' to restore the default mapping"
fi

qmicli -d "$QRTR" --uim-change-provisioning-session \
    "session-type=primary-gw-provisioning,activate=yes,slot=$SLOT,aid=$AID" \
    && log "USIM bound (slot $SLOT)" || log "bind failed"
