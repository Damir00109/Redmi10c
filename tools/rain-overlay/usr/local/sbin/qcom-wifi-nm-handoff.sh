#!/bin/bash
# Deprecated wrapper — use rain-nm-enable.sh (cold-start for stock NM).
# Old handoff (steal live wpa session) reboots this board.
exec /usr/local/sbin/rain-nm-enable.sh "$@"
