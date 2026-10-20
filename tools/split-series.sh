#!/usr/bin/env bash
# Split kernel.patch into the logical upstream series described in
# docs/UPSTREAM-SERIES.md.  Every file of the input patch ends up in
# exactly one output patch; the script verifies that and fails otherwise.
#
# usage: tools/split-series.sh [outdir]   (default: patches/)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PATCH="$ROOT/kernel.patch"
OUT="${1:-$ROOT/patches}"

[ -f "$PATCH" ] || { echo "missing $PATCH" >&2; exit 1; }

series_for() {
	local f="$1"
	case "$f" in
	# --- generic, not SM6225 specific -------------------------------------
	drivers/net/wireless/ath/ath10k/wmi.c)          echo "01-generic-fixes" ;;
	drivers/clk/clk.c)                              echo "01-generic-fixes" ;;
	drivers/clk/clk-bulk.c)                         echo "01-generic-fixes" ;;
	sound/soc/qcom/qdsp6/q6dsp-lpass-clocks.c)      echo "01-generic-fixes" ;;
	drivers/firmware/qcom/qcom_scm.c)               echo "01-generic-fixes" ;;

	# --- boot log noise (pr_info/pr_err -> pr_dbg) ------------------------
	block/partitions/core.c)                        echo "02-quiet-boot" ;;
	drivers/base/core.c)                            echo "02-quiet-boot" ;;
	drivers/regulator/core.c)                       echo "02-quiet-boot" ;;
	drivers/remoteproc/qcom_q6v5.c)                 echo "02-quiet-boot" ;;
	drivers/scsi/*)                                 echo "02-quiet-boot" ;;

	# --- CAMSS / sensors ---------------------------------------------------
	drivers/media/*)                                echo "03-camss-sm6225" ;;

	# --- pinctrl -----------------------------------------------------------
	drivers/pinctrl/qcom/*)                         echo "04-pinctrl-khaje" ;;

	# --- storage -----------------------------------------------------------
	drivers/ufs/*)                                  echo "05-ufs-khaje" ;;
	drivers/phy/qualcomm/phy-qcom-qmp-ufs.c)        echo "05-ufs-khaje" ;;

	# --- modem data path ---------------------------------------------------
	drivers/net/ipa/*)                              echo "06-ipa-sm6225" ;;

	# --- clock / interconnect / soc ---------------------------------------
	drivers/clk/qcom/*)                             echo "07-clk-interconnect-soc" ;;
	include/dt-bindings/clock/qcom,gcc-sm6115.h)    echo "07-clk-interconnect-soc" ;;
	drivers/interconnect/*)                         echo "07-clk-interconnect-soc" ;;
	drivers/soc/qcom/*)                             echo "07-clk-interconnect-soc" ;;

	# --- audio / soundwire / bluetooth ------------------------------------
	sound/soc/*)                                    echo "08-audio-sm6225" ;;
	drivers/soundwire/qcom.c)                       echo "08-audio-sm6225" ;;
	drivers/bluetooth/hci_qca.c)                    echo "08-audio-sm6225" ;;

	# --- display / gpu -----------------------------------------------------
	drivers/gpu/*)                                  echo "09-display-gpu" ;;

	# --- usb ---------------------------------------------------------------
	drivers/usb/*)                                  echo "10-usb" ;;
	drivers/phy/qualcomm/phy-qcom-qusb2.c)          echo "10-usb" ;;
	drivers/phy/qualcomm/phy-qcom-snps-femto-v2.c)  echo "10-usb" ;;

	# --- newly written drivers --------------------------------------------
	drivers/power/supply/*)                         echo "11-new-drivers" ;;
	drivers/misc/*)                                 echo "11-new-drivers" ;;
	drivers/input/touchscreen/*)                    echo "11-new-drivers" ;;
	drivers/i2c/busses/*)                           echo "11-new-drivers" ;;
	drivers/tty/serial/*)                           echo "11-new-drivers" ;;

	# --- board / soc DT (goes last) ---------------------------------------
	arch/arm64/boot/dts/qcom/*)                     echo "12-dt-sm6225-fog" ;;

	*)                                              echo "99-misc" ;;
	esac
}

rm -rf "$OUT"
mkdir -p "$OUT"

# Split the monolithic patch into one file per target path.
python3 - "$PATCH" "$OUT" <<'PY'
import sys, os, re
patch, outdir = sys.argv[1], sys.argv[2]
text = open(patch, encoding='utf-8', errors='surrogateescape').read()
parts = re.split(r'(?m)^(?=diff --git )', text)
chunks = [p for p in parts if p.startswith('diff --git ')]
os.makedirs(outdir + '/.per-file', exist_ok=True)
for p in chunks:
    m = re.match(r'diff --git a/(\S+) b/', p)
    name = m.group(1).replace('/', '__')
    open(f"{outdir}/.per-file/{name}", 'w', encoding='utf-8',
         errors='surrogateescape').write(p)
print(len(chunks))
PY

count=0
for f in "$OUT"/.per-file/*; do
	src=$(basename "$f" | sed 's|__|/|g')
	s=$(series_for "$src")
	cat "$f" >> "$OUT/$s.patch"
	count=$((count + 1))
done
rm -rf "$OUT/.per-file"

echo "files split: $count"
ls -1 "$OUT" | sed 's/^/  /'
