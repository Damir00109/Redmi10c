#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Extract Android DT from Lineage backup images and merge rain overlay.
# Output: notes/dt-adapt/from-images/
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ACT="$ROOT/backup/ab-20260726-023543/active"
OUT="$ROOT/notes/dt-adapt/from-images"
mkdir -p "$OUT"/{vendor_boot,boot,dtbo,merged}

echo "== unpack boot =="
rm -rf "$OUT/boot/unpacked" && mkdir -p "$OUT/boot/unpacked"
unpack_bootimg --boot_img "$ACT/boot.img" --out "$OUT/boot/unpacked" | tee "$OUT/boot/info.txt"

echo "== extract vendor_boot DTBs =="
python3 - <<PY
from pathlib import Path
import struct
p=Path("$ACT/vendor_boot.img")
d=p.read_bytes()
assert d[:8]==b"VNDRBOOT"
page_size=struct.unpack_from("<I",d,12)[0]
vrd_size=struct.unpack_from("<I",d,24)[0]
dtb_size=struct.unpack_from("<I",d,2100)[0]
off=page_size
off2=off+((vrd_size+page_size-1)//page_size)*page_size
dtb=d[off2:off2+dtb_size]
out=Path("$OUT/vendor_boot")
(out/"dtb_blob.bin").write_bytes(dtb)
magic=b"\xd0\x0d\xfe\xed"
i=n=0
while True:
    j=dtb.find(magic,i)
    if j<0: break
    sz=int.from_bytes(dtb[j+4:j+8],"big")
    if 256<sz<=len(dtb)-j:
        (out/f"base-{n:02d}.dtb").write_bytes(dtb[j:j+sz]); n+=1
    i=j+4
print(f"split {n} base dtb(s), blob={len(dtb)}")
PY

echo "== extract dtbo entry 59 (rain board-id 0x60022) =="
python3 - <<PY
from pathlib import Path
data=Path("$ACT/dtbo.img").read_bytes()
magic=b"\xd0\x0d\xfe\xed"
offs=[]; i=0
while True:
    j=data.find(magic,i)
    if j<0: break
    offs.append(j); i=j+4
o=offs[59]; sz=int.from_bytes(data[o+4:o+8],"big")
Path("$OUT/dtbo/rain-60022.dtbo").write_bytes(data[o:o+sz])
print(f"dtbo[59] size={sz}")
PY

echo "== merge base + rain overlay =="
fdtoverlay -i "$OUT/vendor_boot/base-00.dtb" -o "$OUT/merged/rain-from-images.dtb" \
  "$OUT/dtbo/rain-60022.dtbo"
dtc -I dtb -O dts -o "$OUT/merged/rain-from-images.dts" "$OUT/merged/rain-from-images.dtb"
# Clean UFS node extract
python3 - "$OUT/merged/rain-from-images.dts" "$OUT/merged/ufs-nodes.dts" <<'PY'
from pathlib import Path
import re, sys
text = Path(sys.argv[1]).read_text()
parts = []
for n in ["ufsphy_mem@4807000", "ufshc@4804000",
          "ufs_dev_reset_assert", "ufs_dev_reset_deassert"]:
    m = re.search(r"(?m)^(\t+)" + re.escape(n) + r"\s*\{", text)
    if not m:
        print("miss", n); continue
    start = m.start(); depth = 0; j = m.end() - 1
    while j < len(text):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                k = j + 1
                if k < len(text) and text[k] == ";":
                    k += 1
                parts.append(text[start:k])
                break
        j += 1
hdr = ("/* Android rain UFS from vendor_boot + dtbo[59] */\n"
       "/* vddp=1.232V vcc=2.95-2.96V vccq2=1.8V phy=0xe00 lanes=2 */\n\n")
Path(sys.argv[2]).write_text(hdr + "\n\n".join(parts) + "\n")
print("ufs-nodes parts", len(parts))
PY
ln -sfn from-images/merged/rain-from-images.dtb "$ROOT/notes/dt-adapt/rain-from-images.dtb"
ls -lh "$OUT/merged/rain-from-images."* "$OUT/merged/ufs-nodes.dts"
echo "OK: $OUT/merged/rain-from-images.dtb"
