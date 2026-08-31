#!/bin/bash
# NetworkManager cannot coexist with ath10k_snoc + MPSS modem on rain/fog.
# Evidence (2026-08-01):
#   - systemctl start NetworkManager while wlan/wpa live → soft hang
#   - qcom-wifi-start while NM active (even unmanaged-devices=*) → soft hang
#   - NM alone with modem offline → OK, but useless for Wi-Fi
#
# Stock NM still constructs wifi devices / opens nl80211 (nm-device-wifi.c)
# despite unmanaged flags — that path wedges this SoC.
#
# Internet:  sudo qcom-wifi-start.sh && sudo qcom-wifi-connect.sh
# Do not start NetworkManager on this board.
echo "REFUSED: NetworkManager + Qualcomm Wi-Fi stack = soft hang on this SoC."
echo "Use: sudo qcom-wifi-start.sh && sudo qcom-wifi-connect.sh"
echo "Keep NM masked. Experimental only: FORCE_NM_ONLY=1 $0  (modem must stay OFF)"
[ "${FORCE_NM_ONLY:-0}" = 1 ] || exit 2
exec /usr/local/sbin/rain-nm-safe-start.sh --full
