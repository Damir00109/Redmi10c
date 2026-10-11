#!/bin/bash
set -e
for i in $(seq 1 30); do
    active=$(qbootctl -a 2>/dev/null | sed -nE 's/(Active|Current) slot: //p')
    active=${active#_}
    [ -n "$active" ] && exec qbootctl -m "$active"
    sleep 1
done
echo 'Cannot determine active slot' >&2
exit 1
