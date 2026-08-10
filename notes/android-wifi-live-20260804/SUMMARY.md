# Android Wi-Fi passport — 2026-08-04

**Device:** rain / Lineage 23.2 Motregen 4.19.325 / slot `_b`  
**Source:** live Magisk `su` dump (ADB)

## Confirmed working stack

| Item | Value |
|------|--------|
| Host | `c800000.qcom,icnss` → driver `icnss` (built-in) |
| Client | qcacld `wlan` **5.2.022.12B** / FW **3.2.4.0.1022.0** / HW `40690000` |
| iface | `wlan0` UP + `p2p0` + `wifi-aware0` |
| BDF | `bd3qvdfu.bin` |
| IOMMU | group 5, stream `apps_smmu 0x1a0` |
| CE IRQs | GIC SPI 358–369 → mpm 390–401 (`WLAN_CE_0`…`11`) live |
| Userspace | `cnss-daemon`, `wifi_hal_legacy`, `wificond`, `wpa_supplicant` |
| Modem | `vendor.peripheral.modem.state=ONLINE`, `6080000.qcom,mss` |
| **IPA** | **`5800000.qcom,ipa` + `soc:ipa_smmu_wlan` + `rmnet_ipa0` UP**, `gIPAConfig=0x7d` |

## DT props on live icnss (vs our mainline before fix)

| Prop | Android | Mainline (pre-fix) |
|------|---------|----------------------|
| `iommus` | `0x1a0 0x1` | same |
| `qcom,iommu-dma` | `fastmap` | same |
| `qcom,iommu-dma-addr-pool` | `0xa0000000 0x10000000` | same |
| `qcom,iommu-faults` | `stall-disable`, `HUPCF` | **missing** |
| `qcom,iommu-geometry` | `0xa0000000 0x10010000` | **missing** |
| `smmu_iova_ipa` | `0xb0000000 0x10000` | same |
| IPA CB | `ipa_smmu_wlan` present | **no IPA stack** |

## Implication for hang on `wlan0 up` / RX fill

Android Wi-Fi datapath depends on **IPA uC** (`gIPAConfig=0x7d` bits 0/5/6).  
Our Ubuntu image reused that INI and built qcacld with `CONFIG_IPA_OFFLOAD=y` **without** IPA/SMMU WLAN — classic soft-hang / RCU stall on RX DMA path.

## Fixes applied 2026-08-04

1. `WCNSS_qcom_cfg.ini` → `gIPAConfig=0` on Linux rootfs / overlay  
2. DT `sm6225.dtsi` `&icnss`: add geometry/faults + `wlan-ipa-disabled`  
3. `build-qcacld.sh`: force `CONFIG_IPA_OFFLOAD=n` (needs rebuild of `wlan.ko`)

## Next test (on Linux)

```bash
# after boot to Ubuntu dualboot:
sudo qcom-modem-minimal.sh   # or modem-start
# push new ini + rebuilt wlan.ko if needed
sudo qcom-icnss-load.sh
# wait for wlan0, then:
ip link set wlan0 up
iw dev wlan0 scan | head
```
