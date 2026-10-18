#!/bin/bash
# ModemManager 1.25.95 on this modem SIGSEGVs when it connects while a
# qmapmux link from a previous instance is still present (AGENTS, 2026-10-11).
# Drop leftovers before every ModemManager (re)start: a link can only be
# stale at that point, because MM itself owns the connection that creates it.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

for l in $(ip -o link show 2>/dev/null | awk -F': ' '/qmapmux/ {print $2}'); do
  if ip link del "$l" 2>/dev/null; then
    echo "mm-qmapmux-cleanup: removed stale $l"
  else
    echo "mm-qmapmux-cleanup: could not remove $l" >&2
  fi
done
exit 0
