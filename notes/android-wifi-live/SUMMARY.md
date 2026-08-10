# Android Wi-Fi passport — Redmi 10C NFC (rain)

**Date:** 2026-07-29  
**Device:** `rain` / 220333QNY / slot `_b` / platform `bengal`  
**Source:** live Magisk dump → `notes/android-wifi-live/`

## Chip / stack (confirmed)

| Item | Value |
|------|--------|
| WLAN host | **`qcom,icnss` @ `0xc800000`** |
| WLAN driver | **qcacld** (`wlan` / HDD) **v5.2.022.12B** |
| WLAN FW | **FW:3.2.4.0.1022.0** |
| HW id | **`HW_VERSION=40690000`** (`vendor.wlan.firmware.version`) |
| Foundry prop | `ro.wlan.chip=TSMC` |
| BT companion | **`qca,wcn3990`** + slim `a5c0000.slim:wcn3990` |
| Interface | **`wlan0`** (`wlan.driver.status=ok`) |
| OEM MAC prop | `ro.ril.oem.wifimac=f0:6c:5d:02:36:a2` |
| BDF | **`bd3qvdfu.bin`** (`ro.wlan.bdf`) on `firmware_mnt` |
| MSA carveout | `wlan_msa_region@51900000` |
| Modem PIL | `6080000.qcom,mss` + `modem_b` → `/vendor/firmware_mnt` |

## Runtime services

- `cnss-daemon` running  
- `vendor.wifi_hal_legacy` running  
- `wificond` running  

## Firmware layout

- `/vendor/firmware/wlan/qca_cld/WCNSS_qcom_cfg.ini` → `/vendor/etc/wifi/WCNSS_qcom_cfg.ini`  
- `/vendor/firmware/wlan/qca_cld/wlan_mac.bin` → `/mnt/vendor/persist/wlan_mac.bin`  
- Board data / BDFs under `/vendor/firmware_mnt/image/` (`bd3qvdfu.bin`, `bdwlan.*`, …)  
- Copied locally: `fw/WCNSS_qcom_cfg.ini`, `fw/bd3qvdfu.bin`, `fw/wlan_mac.bin` (if pull ok)

## Mainline implication

**Not** a simple ath10k/ath11k board enable.

Path for native Linux:

1. Port / reuse **ICNSS** + **qcacld-3.0** (or equivalent out-of-tree), **or**  
2. Long-shot upstream CNSS/WLAN for this HW id — unlikely short-term.

Hard dependencies before `wlan0`:

- modem / PIL / QRTR (or icnss-only bringup if possible)  
- SMMU + `wlan_msa` reserved memory  
- IPA SMMU WLAN CB (Android links `ipa_smmu_wlan`)  
- BDF + `WCNSS_qcom_cfg.ini` + MAC  

**Practical internet on mainline sooner:** USB tether (RNDIS/ECM) from PC.

## Dump files

| File | Content |
|------|---------|
| `props/getprop-wifi.txt` | wifi/wlan props |
| `dmesg-wifi.txt` | icnss/wlan/PIL lines |
| `sysfs/platform-wifi.txt` | icnss / wcn3990 devices |
| `sysfs/firmware-wlan-ls.txt` | firmware_mnt listing |
| `fw/` | cfg + BDF + mac |
| `META.txt` | device/build stamp |
