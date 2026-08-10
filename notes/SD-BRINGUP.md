# SD card bringup — rain/fog (sdhc2 / µSD)

Status (2026-08-03): **works** — 16 GB card at UHS-I SDR104.

## Hardware
| Piece | Detail |
|-------|--------|
| Controller | `sdhci@4784000` (SDHC2), `qcom,sm6115-sdhci` / `qcom,sdhci-msm-v5` |
| Bus | 4-bit, pads `sdc2_clk/cmd/data` |
| CD | Android `gpio88` ACTIVE_LOW — still in `khaje_reserved_gpios` (EAST); bringup uses `broken-cd` |
| VDD | `pm6125_l22` (~3.0 V) → `vmmc-supply` |
| VDD-IO | `pm6125_l5` (1.8–3.0 V) → `vqmmc-supply` |

## Live result
```
mmc0: new UHS-I speed SDR104 SDHC card
mmcblk0: mmc0:0001 SD16G 14.5 GiB
mmcblk0: p1 p2
clock ~198 MHz, bus 4-bit, signal 1.8 V
```

## DT
- SoC node: `sm6225.dtsi` → `sdhc_2: mmc@4784000` + `sdc2_state_on/off`
- Board: `sm6225-xiaomi-fog.dts` → `&sdhc_2 { ... status = "okay"; broken-cd; }`

## Next
- Unreserve TLMM gpio88 (if EAST `get_direction` is safe) and switch to `cd-gpios`
- Optional: interconnect votes (omitted for now; SDR104 already works)
