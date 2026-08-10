# Wi‑Fi port notes — rain / WCN3990

## Path chosen

**Mainline `ath10k_snoc` + modem PAS**, not Android `qcacld`/`icnss`.

Android uses `qcom,icnss` + qcacld-3.0. Upstream for the same silicon is
`compatible = "qcom,wcn3990-wifi"` (see `sm6115.dtsi`).

## DT (this tree)

- `sm6225.dtsi`: `wifi@c800000`, `remoteproc@6080000` (mpss), `smp2p-mpss`
- `sm6225-xiaomi-fog.dts`: enables wifi supplies (l8/l16/l17/l23) + mpss
  `firmware-name = "qcom/sm6225/modem.mdt"`
- Display MDSS stays disabled (simplefb)

## Firmware in initramfs

| Path | Source |
|------|--------|
| `ath10k/WCN3990/hw1.0/{board-2,firmware-5,wlanmdsp.mbn}` | linux-firmware |
| `qcom/sm6225/modem.*` | phone `firmware_mnt` (~50MB) |
| `qcom/sm6225/wlan/bd3qvdfu.bin` | Android BDF (for later board-2) |

Rebuild: `tools/stage-wifi-firmware.sh`

## Image

`out/boot-ufs-wifi1-ath10k.img` — flash when phone in fastboot
(`adb reboot bootloader` from Android).

## Expected bringup order

1. `qcom_q6v5_pas` loads modem.mdt  
2. ath10k_snoc TQFTPs `wlanmdsp.mbn` via QMI  
3. `wlan0` appears  

Likely first failures: modem auth/PIL, missing IPA/SMMU bits, wrong
board-2 calibration → iterate from dmesg.
