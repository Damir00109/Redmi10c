# Redmi 10C rain — driver inventory (from live device)

Captured: 2026-07-26
Kernel: 4.19.325 Motregen (almost everything built-in, lsmod empty)
DT model: Qualcomm Technologies, Inc. RAIN KHAJE IDP nopmi

## Important
На этом устройстве почти нет внешних `.ko`.
«Вне ядра» = firmware + userspace HAL.
В ядре драйверы есть, но многие — downstream/CLO, не mainline.

## Map: easy-ish → reverse / hard

**Live on 7.1.5 thin DT (2026-07-28):** CPU/PSCI, simplefb, USB ACM-only, SPMI/pm6125/RPM, UFS `/dev/sd*`, smb1351 charging + sh366101 FG (`bms` SOC%).

| Status | Blocks |
|--------|--------|
| Works now | CPU, simplefb, USB ACM, SPMI/RPM LDOs, UFS, smb1351 charge, sh366101 FG |
| Partial | Charger (no Jeita/Type-C; FG without AFI reload) |
| Hard | Wi‑Fi qcacld/WCN3990, BT slim, audio, cameras, modem |
| No path | Xiaomi fingerprint (FPC+Silead proprietary) |

| Block | What we see | Kernel side | Userspace / FW | Mainline outlook |
|-------|-------------|-------------|----------------|------------------|
| SoC bringup | SM6225 / khaje / bengal | clocks, gcc, pinctrl, RPMH | — | Medium: base often exists for bengal family |
| UART/USB/storage | standard qcom | in-tree | — | Easy (USB proven; UFS next) |
| Display panel | `dsi_panel_c3q_43_03_0b_fhdp_video` | msm drm / dsi | — | Medium: DT + panel timing, often copy from Android DT |
| GPU | Adreno 610 (`a610_zap*`) | Android: KGSL; mainline: msm | a610 firmware | Medium: freedreno/msm + FW extract |
| Touch | Focaltech SPI (`CONFIG_TOUCHSCREEN_FTS_SPI`) + Novatek FW files | downstream FTS/NVT | `focaltech_ts_fw_xinli.bin`, `novatek_ts_*.bin` | Medium-Hard: mainline has focaltech/novatek families, need match IC + FW |
| NFC (rain) | `ro.hardware.nfc_nci=pn8x`, `CONFIG_NFC_NQ=y` | NQ/NXP NCI | `libnfc-nxp*.conf` | Medium-Easy: NXP NCI often mainline-able; PN557 bindings landed in 2026 |
| Wi‑Fi | `qca_cld`, ICNSS, ATH, `bd3qvdfu.bin`, WCN3990 BT companion | **qcacld / icnss** (huge downstream) | WLAN FW under `firmware_mnt` | Hard: ath11k/ath10k path depends on exact WCN; often long fight |
| Bluetooth | `CONFIG_BTFM_SLIM_WCN3990` | slim/btfm downstream | bt_firmware partition | Hard-Medium with Wi‑Fi stack |
| Fingerprint | boot `fpc`, vendor `silead`; both `CONFIG_FPC1020` + `CONFIG_SILEAD_FP` | Xiaomi downstream | `fingerprint.fpc/silead` HAL | Hard for auth; Silead has some mainline gslX680 history but Xiaomi FP stack is proprietary |
| Audio | `audio.primary.bengal`, `aw87xxx`, `fs1599` | bolero/wcd downstream | acf/fsm FW | Hard |
| Cameras | s5kjn1 / ov02b1b / hi556 | CAMSS + sensor | CHI override HAL | Very hard |
| Modem/SMS | QRTR/RPMSG | remoteproc/qrtr | rild / modem ELF | Hard (userspace + FW), kernel pieces exist |
| Charging/PMIC | rain DTBO: **smb1351-charger@55**; model still “nopmi” | downstream smb1351; mainline has SMBB/SMB2 ≠ 1351 | — | Medium-Hard: no charge under our gadget image until port/DT |


## Firmware blobs worth keeping
From `/vendor/firmware`:
- GPU: `a610_zap*`
- Touch: `focaltech_ts_fw_xinli.bin`, `novatek_ts_*.bin`
- IPA: `ipa_fws*`, `scuba_ipa_fws*`
- Amp: `aw87xxx_acf.bin`, `fs1599.fsm`
- WLAN dir: `wlan/qca_cld` + BDF `bd3qvdfu.bin`

Also partitions: `modem_a/b`, `dsp_a/b`, `bluetooth_a/b`, `vendor.firmware_mnt`

## Tomorrow pull list (high value)
1. `/vendor/firmware` + wlan BDF
2. `/vendor/etc/libnfc-nxp*.conf`
3. Android DT / extracted `.dtb` from boot_b
4. Motregen defconfig snippet (already: NFC_NQ, FTS_SPI, SILEAD, FPC, ICNSS, KGSL)
5. Panel node from DT (`c3q_43_03_0b`)

## Practical order for mainline
1. boot + UART/USB + UFS
2. display + simplefb/DRM
3. touch
4. GPU
5. Wi‑Fi/BT
6. audio / modem / camera / FP
7. NFC last (conf already saved; do not block earlier bringup)
