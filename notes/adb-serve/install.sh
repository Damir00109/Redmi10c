#!/bin/bash
set -euo pipefail
BASE=${1:-http://HOST:8765}
cd /tmp
rm -rf rain-adb-inst && mkdir rain-adb-inst && cd rain-adb-inst
wget -q -r -np -nH --cut-dirs=0 -R index.html "$BASE/" || curl -fsSL "$BASE/manifest.txt" >/dev/null
