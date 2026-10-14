#!/bin/sh
# rain-gps-start.sh — bring up the GNSS stack and apply the A-GPS config.
#
# The GNSS engine lives in the modem (QMI LOC/PDS, QRTR service 16).
# ModemManager exposes it as the Location interface; gnss-share republishes
# the NMEA on /var/run/gnss-share.sock for geoclue/apps.
#
# On Android the vendor IZat HAL pushes a bundle of settings into the modem
# (see the stock /vendor/etc/gps.conf: AGPS_CONFIG_INJECT=1, CAPABILITIES=0x17
# -> MSA|MSB, LPP_PROFILE=3, A_GLONASS_POS_PROTOCOL_SELECT=0xf, XTRA + NTP).
# ModemManager only implements part of that and this firmware never sends the
# indications MM waits for, so the bundle is applied by /usr/local/bin/rain-loc-tool
# (libqmi, built from tools/rain-loc-tool.c):
#
#   time   — inject the AP UTC time (gps.conf: XTRA_SYSTEM_TIME_INJECT=1);
#            without it the engine stays in a measurement-only state
#            (GSV with empty PRNs, no GGA)
#   orbits — inject predicted orbits (XTRA) fetched over the network
#   server — SUPL server (A-GPS needs it)
#   mode   — MSB/MSA operation mode
#
# Ordering: the LOC calls disturb MM's own LOC session (its NMEA stream stops),
# so the config is applied first and ModemManager is restarted afterwards,
# then the location gathering is enabled for the final state.
#
# A position fix still needs a sky view: behind window glass the engine
# tracks ~10 SVs at 30-39 dBHz, reports GGA with fix quality 0 and no fix.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

log() { echo "[rain-gps] $*" >&2; }

TOOL=/usr/local/bin/rain-loc-tool
XTRA=/var/lib/xtra.bin
XTRA_URL="${XTRA_URL:-https://path2.xtracloud.net/xtra3grcej.bin}"
SUPL="${SUPL_SERVER:-supl.google.com}"
SUPL_PORT="${SUPL_PORT:-7275}"

wait_modem() {
    for i in $(seq 1 60); do
        mmcli -L 2>/dev/null | grep -q Modem && return 0
        sleep 2
    done
    return 1
}

enable_location() {
    for f in gps-nmea gps-raw 3gpp; do
        mmcli -m 0 --location-enable-"$f" >/dev/null 2>&1 && log "enabled $f"
    done
    for f in agps-msa agps-msb; do
        mmcli -m 0 --location-enable-"$f" >/dev/null 2>&1 \
            || log "$f refused by MM (firmware does not send the indication)"
    done
}

wait_modem || { log "no modem in ModemManager"; exit 0; }

# The Location interface can only be configured on an enabled modem.
state=$(mmcli -m 0 2>/dev/null | sed -n 's/.*state: *//p' | head -1)
case "$state" in
    disabled|"") mmcli -m 0 --enable >/dev/null 2>&1 && log "modem enabled (was: ${state:-unknown})" ;;
esac

# --- A-GPS configuration (the HAL's job on Android) ---
if [ -x "$TOOL" ]; then
    "$TOOL" time   >/dev/null 2>&1 && log "UTC time injected"
    "$TOOL" server "$SUPL" "$SUPL_PORT" >/dev/null 2>&1 && log "SUPL server $SUPL set"
    "$TOOL" mode msb >/dev/null 2>&1 && log "operation mode MSB set"

    # Predicted orbits (XTRA): fetch at most once a day, then inject.
    if [ ! -s "$XTRA" ] || [ -n "$(find "$XTRA" -mmin +1440 2>/dev/null)" ]; then
        curl -sL --max-time 30 -o "$XTRA" "$XTRA_URL" 2>/dev/null \
            && log "XTRA orbits fetched" || log "XTRA fetch failed (no network?)"
    fi
    if [ -s "$XTRA" ]; then
        "$TOOL" orbits "$XTRA" >/dev/null 2>&1 && log "XTRA orbits injected"
    fi
else
    log "rain-loc-tool not found — A-GPS config not applied"
fi

# The LOC calls above stop MM's NMEA stream; restart it before enabling.
systemctl --no-block try-restart ModemManager.service
sleep 45
wait_modem || { log "modem did not come back"; exit 0; }

enable_location
mmcli -m 0 --location-status 2>/dev/null | grep -E "capabilities|enabled" | sed 's/^/  /' >&2

# Держим сессию открытой (иначе движок глохнет без клиента). Поднимается
# здесь, а не из загрузки: он ждёт этот провижининг, и в multi-user.target
# держал boot несколько минут.
systemctl --no-block start gnss-keepalive.service 2>/dev/null
exit 0
