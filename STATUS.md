# Status — 2026-08-05 (evening)

## 2026-08-15

### NFC: driver probes, chip unresponsive (50%)
- NXP NQ-NCI NFC chip on **i2c2** (`i2c@4a88000`, SE2) at address **0x2a** (not 0x28/i2c1 as initially assumed).
- DTS node `nq-nci@2a` added to `sm6225-xiaomi-fog.dts` under `&i2c2` with:
  - IRQ: GPIO 70 (EDGE_FALLING)
  - VEN (enable): GPIO 56
  - FIRM: GPIO 31
- GPIOs 31/56/70 unreserved from `khaje_reserved_gpios` in `pinctrl-khaje.c`.
- `nxp-nci_i2c` module loads, probes successfully, `nfc0` + `rfkill0` appear in sysfs.
- `NFC_CMD_DEV_UP` via genlink raises VEN (GPIO 56 high), but NCI RESET times out (`__nci_request: wait_for_completion_interruptible_timeout failed 0`).
- Chip ACKs zero-length I2C writes after ~200ms boot delay, but **any data write (incl. NCI RESET 0x20 0x00 0x01 0x00) causes phone reboot** — likely I2C bus hang / watchdog.
- In **stock Android** (slot A): `nq-nci 1-002a: nfcc_hw_check: - NFCC HW not Supported`, `NxpFwDnld: Error loading libpn54x_fw`, `Wrong FW Version >>> Firmware download not allowed`. NFC service reports `mState=on` but firmware download fails. 37 IRQ interrupts registered.
- **Conclusion**: chip is partially functional (ACKs I2C address, generates IRQ in Android) but cannot complete NCI initialization — likely needs firmware download that Android also fails to do. Mainline `nxp-nci` driver doesn't support FW download for NQ-NCI. Further work would require porting downstream `qcom,nq-nci` driver or finding/extracting NFC firmware.

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
| GPU Adreno 610 | **kernel 100%** — PLL, DRM, Vulkan driver; **3D render crashes** | 70% |
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
| GPU Adreno 610 | kernel OK, Vulkan driver OK, **3D render = crash** | 70% |
| Display (DRM/DSI + panel) | **works** — 720x1650, backlight OK, fbcon on DRM | OK |
| Bluetooth | WCN3990 BTFM, не начато | 0% |
| Audio codec / speakers | не начато | 0% |
| Camera | не начато | 0% |
| Sensors | не найдены в DT/I2C | 0% |
| GPS | не начато | 0% |
| Modem (voice/data) | не начато | 0% |
| Fingerprint | проприетарный FPC/Silead | 0% |
| NFC | driver probes, chip ACKs I2C but NCI init fails (FW issue) | 50% |

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

### 3D rendering — kernel + Vulkan driver work, but **display path crashes** (2026-08-14)

**What works:**
- `vulkaninfo --summary` → `Turnip Adreno (TM) 610`, Mesa 25.2.8 ✅
- Headless Vulkan: device creation, memory alloc, buffer binding ✅
- **Headless Vulkan 3D render**: `vkCreateImage`, `vkAllocateMemory`,
  `vkBindImageMemory`, `vkCmdClearColorImage`, `vkQueueSubmit`,
  `vkQueueWaitIdle` — **ALL WORK, no errors, no crash** ✅
- Weston with Pixman (software) renderer ✅ — DSI-1 720x1650, touch, backlight
- `vkcube-wayland` finds GPU: `Selected GPU 0: Turnip Adreno (TM) 610`
- Weston GL renderer: EGL 1.5 init, Mesa OpenGL ES — **starts OK** ✅

**What crashes (hardware lockup, NOT panic):**
- `weston --renderer=gl`: hangs ~2s after EGL init (during first swap buffers)
- `weston-simple-egl`: phone hangs
- `weston-simple-dmabuf-egl`: phone hangs
- `vkcube-wayland` under pixman weston: `demo_init_vk_swapchain: Assertion
  '!err'` (swapchain creation fails — expected, needs GL renderer)

**Android comparison (2026-08-14):**
Booted into Android (slot a) to verify GPU hardware. Android uses:
- Kernel 4.19.325 (downstream Qualcomm)
- **kgsl** proprietary driver (not mainline freedreno)
- msm_drm (downstream) for display

Results in Android:
- AdrenoGLES (OpenGL ES) — works perfectly
- AdrenoVK (Vulkan) — works perfectly
- WebGL in browser — works
- **NO SMMU faults, NO crashes, NO hangs**
- `kgsl_worker_thr` active, no errors in logcat

**Conclusion: GPU hardware is 100% functional. The crash is 100% a
mainline driver issue.**

**Root cause analysis (2026-08-14):**
The crash is a **hardware lockup** (CPU stuck on MMIO read), NOT a kernel
panic. RCU stall fires after ~60s but doesn't cause panic (no
SOFTLOCKUP_DETECTOR). pstore/ramoops can't capture it because:
1. The lockup is a hard CPU stall — no code runs to write pstore
2. ramoops DTS node at 0x45d00000 **breaks boot** (conflicts with System RAM
   used by UFS/cust mount) — had to revert
3. journald can't flush to UFS during lockup

The crash happens when **GPU renders to a buffer and DPU tries to scanout
from it** (atomic commit with GPU GEM buffer). Headless GPU rendering
(without display) works perfectly. This points to:
1. **DPU scanout from GPU buffer** — possible SMMU context fault when DPU
   reads GPU-allocated memory
2. **GMU wrapper power management** — `sync_state() pending due to
   596a000.gmu`, GPU power domains may not be properly enabled for rendering
3. **Zap shader** — `a610_zap.mdt` present in `/lib/firmware/qcom/sm6225/`
   but QSEECOM skipped (`untested machine`), so secure rendering may not work

**SMMU faults observed (boot -1, kernel #116):**
```
arm-smmu c600000.iommu: Unhandled context fault: fsr=0x402, iova=0x5c000000,
  fsynr=0x350021, cbfrsynra=0x420, cb=3
```
SID=0x420, iova=0x5c000000 = cont_splash_mem (framebuffer). These are
APPS SMMU faults, not adreno_smmu — likely DPU trying to scanout splash
memory without proper SMMU mapping.

**Installed on phone (survives reboot):**
- `/usr/lib/aarch64-linux-gnu/libvulkan_freedreno.so` (Turnip ICD)
- `/usr/share/vulkan/icd.d/freedreno_icd.json`
- `/usr/local/bin/vulkaninfo`, `vkcube`, `vkcube-wayland`
- `/lib/firmware/qcom/sm6225/a610_zap.{mdt,b00,b01,b02,elf,mbn}`

### Remaining for 100% GPU
- Fix display path crash (DPU scanout from GPU buffer → hardware lockup)
- Investigate SMMU context faults (SID=0x420, iova=0x5c000000)
- `supply vdd/vddcx not found, using dummy regulator` — may need real regulators
- `sync_state() pending due to 596a000.gmu` — GMU wrapper has no driver
- QSEECOM skipped (`untested machine`) — may need to add SM6225 to allowlist
  for zap shader loading

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
