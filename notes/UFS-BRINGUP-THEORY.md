# Rain (Redmi 10C) — UFS / drivers theory pack

Updated: 2026-07-26 (phone on Lineage charging; no flash)

Sources:
- Live experiments (`logs/ufs-*.txt`, ACM dumps)
- Mainline tree edits (`sm6225*.dts*`, `phy-qcom-qmp-ufs.c`)
- Spes/khaje Android: `ref/kernel_xiaomi_spes`
- **This phone’s Lineage DTBO** unpacked → `notes/dt-extract/dtbo-parts/59.dts`
  (`qcom,msm-id = <0x206>`, `qcom,board-id = <0x60022>` — matches our thin DT)

---

## Device identity (confirmed)

- Flash: **WDC/SanDisk SDINDDK4-64G**, JEDEC **UFS 2.2** (not UFS 5.x)
- Host SoC PHY IP name **QMP v4** (`qcom,ufs-phy-qmp-v4`) ≠ “UFS version 4/5”
- Mainline DT already: `jedec,ufs-2.0` (covers 2.x family); Android on this phone reaches **HS-G3 / 2-lane** then WriteBooster
- Failure remains **link startup (UIC 0x16)** before any version negotiation with the NAND package matters



| Stage | Result | Artifact |
|-------|--------|----------|
| USB ACM + RNDIS | OK | `out/boot-net1.img` |
| SPMI + pm6125 + RPM LDO | OK | (in v4tbl stack) |
| Wrong PHY (PCS v2 on v4 silicon) | HCE fail / false ready | pre-v4pcs |
| PCS layout v4 only | real `phy init timed-out -110` | `boot-ufs-v4pcs.img` |
| Spes qmp-v4 tables + L18@1.232V | **HCE=0x3**, `scsi host0: ufshcd` | `boot-ufs-v4tbl.img` |
| Link startup | **UIC 0x16 timeout → link startup -110** | same |
| TLMM + `reset-gpios` gpio113 | crash → fastboot | `boot-ufs-rst.img` |
| TLMM alone | packed, **never verified** (battery / restore) | `out/boot-ufs-tlmm.img` |
| Lineage restore | OK | `tools/restore-android.sh` |

Success criteria next session: `/dev/sd*` + ACM still up.

---

## Proven theories

### T1 — PHY is QMP **v4**, not mainline sm6115 PCS-v2
Android rain DTBO fragment@35: `compatible = "qcom,ufs-phy-qmp-v4"`.
Mainline bound sm6115 to PCS v2 `READY @ 0x168`; on v4 that offset is gear band → false ready → HCE fail.
**Fix kept in tree:** `ufsphy_v4_regs_layout` + spes rate-A no-G4 init tables in `phy-qcom-qmp-ufs.c` (`sm6115_ufsphy_cfg`).

### T2 — Rails from **this** DTBO (not L12 PLL myth)
Rain `59.dts` symbols:

| Rail | Role |
|------|------|
| L4A | `vdda-phy` |
| L18A | `vdda-pll` **and** `qcom,vddp-ref-clk` @ **1.232 V** |
| L24A | `vcc` @ **2.950–2.960 V** |
| L11A | `vccq2` @ 1.8 V |
| `gcc_ufs_phy_gdsc` | `vdd-hba` (fixed) |

Our thin DT already uses L4/L18/L24/L11 correctly for phy/pll/vcc/vccq2.
Gaps vs Android:
- `vcc` range still wide (2.704–3.6); Android clamps **2.95–2.96**
- `vdd-hba` we feed L18; Android uses **UFS GDSC** (`vdd-hba-fixed-regulator`)
- Android enables extra regulator **`qcom,vddp-ref-clk-supply`** (L18 @ 1.232V, 100 µA)

### T3 — Mainline **does not** implement `qcom,vddp-ref-clk`
Downstream (`ufs-qcom.c` spes): parse/enable `qcom,vddp-ref-clk`, load-switch on hibern8/link-off.
Mainline `drivers/ufs/` — **zero** hits for `vddp-ref`.
Even if L18 is also `vdda-pll`, timing/enable path differs; ref-clk pad may stay off → device silent on UniPro while HCE looks fine.

### T4 — Rain Android DTBO has **no** UFS `reset-gpios`
Fragment@36 sets supplies only — no `reset-gpios`.
Device reset on khaje is the special TLMM pin **`ufs_reset` = gpio 113** (`pinctrl-sm6115.c`, ctl @ `tlmm+0x78000`), used via downstream pinctrl states (`ufs_dev_reset_*` in bengal-pinctrl) / optional gpiod.
Mainline `ufs-qcom`: reset gpio **optional**. Sibling boards (fxtec/j606f) often omit it.
Our `reset-gpios = <&tlmm 113 …>` + full TLMM crashed; **gpio 82 in spes snippets is panel reset, not UFS**.

### T5 — Current fail mode = device side / link, not HCE
```
scsi host0: ufshcd
uic cmd 0x16 … completion timeout   # DME_LINKSTARTUP
link startup failed -110
HCE=0x00000003
```
Host controller enabled; UniPro link to flash did not complete.

---

## Ranked hypotheses for UIC 0x16 (next experiments)

1. **Missing vddp-ref enable path** (highest leverage)  
   Port minimal `qcom,vddp-ref-clk` enable from spes into mainline `ufs-qcom.c`, or force L18 always-on + document; verify voltage via SPMI before link.

2. **UFS_RESET not toggled after Android left device asleep**  
   Prefer **MMIO poke** of pin 113 (`0x00500000 + 0x78000`) in initramfs / early hook **without** probing full TLMM (avoids crash).  
   Else: bisect `boot-ufs-tlmm.img` (TLMM alone) → then `reset-gpios` ACTIVE_LOW vs HIGH.

3. **VCC not in 2.95–2.96 V window**  
   Clamp `pm6125_l24a` / `vcc-voltage-level` like DTBO.

4. **`vdd-hba` should be GDSC, not L18**  
   Drop bogus L18 as vdd-hba; rely on `power-domains` / GDSC already in SoC DT.

5. **Assumed-enabled `vccq`**  
   Log says vccq missing assumed enabled; rain has no separate vccq (only vccq2). Low priority if Android matches.

6. **PHY tables still incomplete (rate-B / G4)**  
   Unlikely primary (HCE+host up); keep as fallback after reset/refclk.

---

## Driver map (mainline vs need)

### Already in our 7.1.5 bringup stack
| Block | Driver / Kconfig | Notes |
|-------|------------------|-------|
| UFSHC | `CONFIG_SCSI_UFS_QCOM` | works through HCE |
| UFS PHY | `CONFIG_PHY_QCOM_QMP_UFS` | patched tables for khaje v4 |
| GCC (partial) | DT + protected USB clocks | UFS clocks from DT |
| SMMU | `apps_smmu` + `iommus` on HC | USB OK with it |
| SPMI / pm6125 | `SPMI_MSM_PMIC_ARB`, RPM regulators | L4/11/12/15/18/24 |
| Pinctrl | `CONFIG_PINCTRL_SM6115=y` | in config; TLMM node untested alone |
| USB gadget | DWC3 + ACM/RNDIS | keep as debug forever |

### Missing / next for UFS link
| Item | Where | Action |
|------|-------|--------|
| `qcom,vddp-ref-clk` | mainline `ufs-qcom.c` gap | **port from spes** (small) |
| Safe UFS_RESET | TLMM gpio113 or MMIO | MMIO first; TLMM bisect |
| VCC clamp | board DT | 2950–2960 mV |
| GDSC as vdd-hba | board DT | align with DTBO |
| Downstream phy-qcom-ufs-qmp-v4 | spes only | only if mainline tables fail again |

### Charging (why Linux on USB drained battery)
| Item | Android rain | Mainline |
|------|--------------|----------|
| Charger | DTBO `smb1351-charger@55` (`qcom,smb1351-charger`) | **no smb1351** in tree; SMBB/SMB2 ≠ this |
| Fuel gauge | vendor stack / battery_params in DTBO | not wired |
| Result | — | empty `/sys/class/power_supply` → **no charge on gadget USB** |

**Ops rule:** never leave bringup image on `boot` overnight without charger driver or restore to Lineage. Soft fastboot blocked when ABL sees low battery; `busybox poweroff -f` may drop into fastboot (observed).

Restore: `tools/restore-android.sh` ← `backup/ab-20260726-023543/active/{boot,dtbo}.img`.

### Later (not UFS-blocking)
GPU Adreno 610 + `a610_zap*`, DRM/panel `c3q_43_03_0b`, touch FTS/NVT, NFC PN8x, Wi‑Fi qcacld — see `HARDWARE-DRIVERS.md`.

---

## Suggested flash order (when charged ≥30%)

One image per step; restore `boot-ufs-v4tbl.img` or Lineage on fail; Vol− if soft FB blocked.

1. **DT-only:** clamp L24 / vcc-voltage-level; fix vdd-hba; keep v4tbl PHY — `boot-ufs-vcc.img`
2. If still UIC 0x16: **vddp-ref port** in `ufs-qcom.c` + DT property — `boot-ufs-vddp.img`
3. Parallel/bisect: flash existing **`boot-ufs-tlmm.img`** (TLMM alone)
4. If TLMM OK: reset via gpiod 113 (try ACTIVE_LOW then HIGH) **or** initramfs MMIO toggle before ufshcd probe
5. Only then deeper PHY / ICE experiments

---

## Quick reference paths

```
mainline/.../dts/qcom/sm6225.dtsi          # SoC UFS, TLMM stub, GCC, SMMU
mainline/.../dts/qcom/sm6225-xiaomi-fog.dts # board rails
mainline/.../phy/qualcomm/phy-qcom-qmp-ufs.c
mainline/.../ufs/host/ufs-qcom.c           # add vddp-ref here
ref/kernel_xiaomi_spes/.../ufs-qcom.c      # reference enable path
notes/dt-extract/dtbo-parts/59.dts         # rain board-id 0x60022 truth
out/boot-ufs-v4tbl.img                     # best UFS progress
out/boot-ufs-tlmm.img                      # next TLMM bisect
out/boot-net1.img                          # USB-only safe
tools/flash-once.sh / tools/restore-android.sh / tools/acm
```

### Power / fastboot survival
- Charge on **Lineage** (current).
- Enter FB: Vol−+Power, or from Linux `busybox poweroff -f` (may land in FB).
- Avoid soft `reboot-bootloader` on low battery.

---

## ADB extract checklist (Lineage = “first Linux”)

Already have locally: `firmware/from-phone/` (a610_zap, touch FW, IPA, NFC conf, firmware_mnt), Lineage `boot.img`/`dtbo.img`, rain overlay `59.dts`.

**Still worth pulling live (script: `tools/adb-pull-bringup.sh`):**

| Priority | What | Why for mainline (“second Linux”) |
|----------|------|-------------------------------------|
| P0 | Live `/sys/firmware/devicetree` tar | Merged final DT after DTBO — real UFS/rails/gpio |
| P0 | Regulator µV for L4/L11/L18/L24 + ufs* | Exact voltages Android runs |
| P0 | `dmesg` UFS / qmp / vddp | Confirm reset + refclk sequence |
| P0 | debugfs pinctrl `ufs_reset` / gpio113 | Whether reset is driven & polarity |
| P1 | `power_supply` + smb1351 sysfs | Port charging so bringup doesn’t brick battery |
| P1 | `/dev/block/by-name` + cmdline | Slot, partition layout |
| P1 | `/proc/config.gz` UFS/SMB/PINCTRL bits | Downstream options we mirror |
| P2 | Missing WLAN BDF / persist | Later Wi‑Fi |
| P2 | Panel timing leftovers | DRM later |

From **mainline** (when ACM up again): `dmesg`, `devmem` HCE/PHY, `ls /sys/class/regulator`, compare to this Android dump.

## Live ADB confirmation (2026-07-26, Lineage)

Pull: `notes/adb-pull-latest/` (+ `FINDINGS.md`).

- Rails live: **L4=0.88V, L18=1.232V, L24=2.95V, L11=1.8V**, vdd-hba=`gcc_ufs_phy_gdsc`
- Reset: **pinctrl** `dev-reset-assert/deassert` on pin **`ufs_reset`** (LOW=assert); not DT `reset-gpios`
- PHY reg size **0xe00**; Android TLMM @ **0x400000** (`qcom,khaje-pinctrl`) vs our mainline stub @ 0x500000
- Android reaches HS-G3 2-lane quickly after host0 — target sequence known good on this silicon

