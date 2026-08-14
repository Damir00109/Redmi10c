#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
set -euo pipefail
ROOT=/home/damir00109/Desktop/Redmi10c
OUT=$ROOT/logs/diag-pstore/logdump.bin
mkdir -p "$(dirname "$OUT")"
adb wait-for-device
adb shell su -c 'dd if=/dev/block/by-name/logdump bs=1M count=4 2>/dev/null' > "$OUT"
python3 - <<'PY'
from pathlib import Path
p=Path('/home/damir00109/Desktop/Redmi10c/logs/diag-pstore/logdump.bin')
d=p.read_bytes()
print('size',len(d),'magic',d[:16])
if d.startswith(b'RAINDIAG1'):
    text=d.split(b'\0',1)[0].decode('utf-8','replace')
    Path('/home/damir00109/Desktop/Redmi10c/logs/diag-pstore/diag.txt').write_text(text)
    print('wrote diag.txt', len(text))
    print(text[:2000])
else:
    # try find magic
    i=d.find(b'RAINDIAG1')
    print('magic_at', i)
PY
