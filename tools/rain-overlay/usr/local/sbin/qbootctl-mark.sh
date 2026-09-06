#!/bin/bash
set -e
active=$(qbootctl -a 2>/dev/null | sed -n "s/Active slot: //p")
active=${active#_}
[ -n "$active" ] || { echo "Cannot determine active slot" >&2; exit 1; }
exec qbootctl -m "$active"
