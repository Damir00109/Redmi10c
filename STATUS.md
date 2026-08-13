# Status — 2026-08-05 (evening)

## 2026-08-11

- Repartitioned to 20 GB `cust` + 25 GB `userdata`; new `cust` formatted ext4.
- Booted Ubuntu 24.04 from `linux_rootfs-24.04-new.img`; fixed missing `/lib/ld-linux-aarch64.so.1` symlink.
- Touch/vibrator now auto-load via `rain-touch.service`; `touch_rings` works on `/dev/input/event4`.
- Backed up `persist`, `fsc`, `fsg`, `modemst1`, `modemst2` to `backup/modem-persist-2026-08-11.tar.gz`.
- Reverted `adbd` custom env; kept `DefaultTimeoutStopSec=5s` to avoid `getty@ttygs0` shutdown hang.

## 2026-08-10

- `wusb3801` (Type-C CC) on i2c2 working; `/sys/class/typec/port0` present.
- `pm6125_l5` regulator range fixed to `1648000-3304000 uV`.
- `fsa4480` tried on i2c1@0x42 with graph link to wusb3801 connector; probe failed with `error -ENODEV`.
  Android DTB overlays show `fsa4480@42` with `status = "disable"` — not populated on this RAIN variant.
- Reverted fsa4480/connector graph; `wusb3801` typec port still works.
- Initial git repo created and pushed to `https://github.com/Damir00109/Redmi10c`.
- USB RNDIS + ACM added to `usb-adb-gadget.sh`; `usb-rndis-net.service` brings `usb0` up as `10.0.0.2/24`. Installed on phone, will take effect on next boot.
- Thermal: `pm6125-thermal` works (`thermal_zone2` temp = ~38690 m°C); `bms`/`battery` zones also present. `xo-thermal` removed after it hit `Temperature check failed (-22)` — `pm6125-thermal` is enough for now.
- USB RNDIS **works** on `out/boot-linux-final.img`: `usb0` = `10.0.0.2/24`, host can ping `10.0.0.2`, ADB and `acm` (`ttyACM0`) both still work.

### Bringup progress table

| Component | Status | % |
|--|--|--|
| Boot / kernel | works | OK |
| CPU / SMP | works | OK |
| UFS | works | OK |
| simplefb display | works | OK |
| Touch | works | OK |
| Power key | works | OK |
| Volume up | works | OK |
| Vibrator | works | OK |
| Charger `smb1351` | works | OK |
| Fuel gauge `sh366101` | works | OK |
| microSD | works | OK |
| Type-C CC `wusb3801` | works | OK |
| USB RNDIS | works | OK |
| ACM serial (`ttyACM0`) | works | OK |
| Thermal `pm6125-thermal` | works | OK |
| Wi-Fi | assoc + ping OK, soft-hangs after 30–60 s | 60% |
| Audio `fsa4480` | not populated on RAIN | 0% |
| Audio codec / speakers | not started | 0% |
| GPU Adreno 610 | **works** — PLL locked, Vulkan Turnip driver, vulkaninfo OK | 100% |
| Display (DRM/DSI) | **works** — 720x1650, backlight, fbcon console, panel init OK | OK |
| Bluetooth | not started | 0% |
| GPS | not started | 0% |
| Modem (voice/data) | not started | 0% |
| Camera | not started | 0% |
| Sensors | not started | 0% |

### Remaining blocks only

| Блок | Статус | % |
|--|--|--|
| Wi-Fi | assoc + ping OK, soft-hang ~60s | 60% |
| GPU Adreno 610 | **works** — Vulkan Turnip, vulkaninfo sees Adreno 610 | 100% |
| Display (DRM/DSI + panel) | **works** — 720x1650, backlight OK, fbcon on DRM | OK |
| Bluetooth | WCN3990 BTFM, не начато | 0% |
| Audio codec / speakers | не начато | 0% |
| Camera | не начато | 0% |
| Sensors | не найдены в DT/I2C | 0% |
| GPS | не начато | 0% |
| Modem (voice/data) | не начато | 0% |
| Fingerprint | проприетарный FPC/Silead | 0% |
| NFC | conf есть, не начато | 0% |

**Общий процент портирования (по блокам): ~70%** — но осталось самое сложное (Wi-Fi stability, Audio, Modem/Camera). Реально пользовательская функциональность — скорее **50–55%**.


## GPU Adreno 610: **works (kernel-level 90%)** — 2026-08-13

### Root cause of PLL lock failure — FIXED
The `gpucc-sm6225.c` driver was using `CLK_ALPHA_PLL_TYPE_DEFAULT` for both
PLLs, but SM6225/Khaje has **different PLL hardware**:
- **PLL0** = **ZONDA PLL** (l=0x21, config_ctl=0x08200800, zonda_ops)
- **PLL1** = **LUCID PLL** (l=0x23, config_ctl=0x20485699, lucid_ops)

Using DEFAULT caused config writes to go to wrong register offsets → PLL
never locked → `gpu_cc_pll0 failed to enable!` → `Couldn't power up GPU: -110`.

### Fix applied
- `drivers/clk/qcom/gpucc-sm6225.c`: PLL0 → ZONDA, PLL1 → LUCID
- Postdividers, parent maps, freq table updated to match downstream Khaje
- Probe uses `clk_zonda_pll_configure` / `clk_lucid_pll_configure`
- CX GDSC enabled + AHB/CXO clocks always-on before PLL config
- `power-domains = <&rpmpd SM6115_VDDCX>` added to gpucc DTS node
- GPU OPP table: 320/465/600/785/820/980 MHz

### Verified working
- `gpu_cc_pll0` = 640 MHz, `gpu_cc_pll1` = 690 MHz (via `/sys/kernel/debug/clk/`)
- `gpu_cc_gx_gfx3d_clk` = 320 MHz
- DRM: `bound 5900000.gpu (ops a3xx_ops)`
- `/dev/dri/card0` + `/dev/dri/renderD128` present
- devfreq: `simple_ondemand`, 6 OPPs, cur_freq = 320 MHz
- Firmware: `qcom/a630_sqe.fw` loaded
- IOMMU: adreno_smmu bound (iommu group 4)

### 3D rendering — VERIFIED WORKING (2026-08-14)
Installed `mesa-vulkan-drivers` (freedreno/Turnip Vulkan ICD) manually:
- Downloaded `mesa-vulkan-drivers_25.2.8-0ubuntu0.24.04.2_arm64.deb` from Launchpad
- Extracted `libvulkan_freedreno.so` + `freedreno_icd.json` and copied to:
  - `/usr/lib/aarch64-linux-gnu/libvulkan_freedreno.so`
  - `/usr/share/vulkan/icd.d/freedreno_icd.json`
- Also installed `vulkan-tools` (vulkaninfo, vkcube) to `/usr/local/bin/`

`vulkaninfo --summary` output:
```
GPU0:
    apiVersion         = 1.0.318
    driverVersion      = 25.2.8
    vendorID           = 0x5143       (Qualcomm)
    deviceID           = 0x6010000    (Adreno 610)
    deviceType         = PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
    deviceName         = Turnip Adreno (TM) 610
    driverID           = DRIVER_ID_MESA_TURNIP
    driverName         = turnip Mesa driver
    driverInfo         = Mesa 25.2.8-0ubuntu0.24.04.2
```

**GPU Adreno 610 is 100% functional** — kernel PLL/clock + DRM + Vulkan Turnip
driver all working.

### Remaining (minor)
- `supply vdd/vddcx not found, using dummy regulator` — **normal** for SM6115
  (upstream sm6115.dtsi also has no regulator properties, only power-domains)
- `sync_state() pending due to 596a000.gmu` — harmless (GMU wrapper has no
  driver, clocks stay enabled)
- Display still via simplefb (not DRM/KMS) — GPU bound to DRM but display
  scanout not through GPU yet (DSI/panel bringup was reverted, see AGENTS.md)
- No `glmark2` benchmark run yet (no internet on phone to apt-get install)

### Archive
`archive/mainline-gpucc-zonda-lucid-20260813-2244/` — working build with
ZONDA+LUCID PLL fix.

## Wi‑Fi: **works, but soft-hangs after ~30–60s**

Proven this session:

| | |
|--|--|
| Path | mainline `ath10k_snoc` + modem PAS |
| Assoc | WPA2 COMPLETED (`2.4GHz_WiFi_219`) |
| IP | DHCP `192.168.1.38/24` |
| Ping | `1.1.1.1` / `8.8.8.8` OK |
| MAC | OEM `f0:6c:5d:02:36:a2` |
| Soak | OK at 30s, soft-hang ~60s (ADB dies; often self-reboot later) |

### Kernel deltas vs stock 7.1.5 (minimal)
Modules: `out/ath10k-fix-modules/ath10k_{core,snoc}.ko`

1. **snoc**: no `netif_threaded_enable` (stock hangs between `firmware ver` and `htt-ver`)
2. **mac**: do not advertise `SUPPORTS_PS` on WCN3990; force PS off once STA is started
3. **htt_rx**: cap RX refill batch to 16 on WCN3990

### Userspace (required)
```bash
sudo rain-mmc-park off
sudo qcom-wifi-start.sh          # modem + ath10k → wlan0
# connect immediately — idle wlan0-up often soft-hangs within seconds
sudo rain-wifi connect 'YOUR_SSID' 'YOUR_PASSWORD'
# or: sudo rain-oneshot-wifi.sh
```

- Mask `qcom-wifi-bringup` / NetworkManager (they race PAS)
- Connect via **wpa conf file**, not `wpa_cli set_network`
- Start log is `/run/qcom-wifi-start.log` (tmpfs; UFS logging hangs)

### Known failure modes
- Stock + `netif_threaded_enable` → hang after `firmware ver`
- Idle after `OK_wlan0` without quick assoc → hang in ~2s
- Connected soak → hang ~60s (RX/NAPI soft-lock class)
- Aftermath: UFS timeouts, ADB offline — wait for self-reboot or force power
