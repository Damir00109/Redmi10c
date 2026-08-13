# Redmi 10C (rain/fog) mainline bringup — handoff notes

## 2026-08-13: GPU Adreno 610 PLL lock FIXED — ZONDA + LUCID PLL types

The persistent `gpu_cc_pll0 failed to enable!` / `Couldn't power up the GPU: -110`
errors are **fixed**. Root cause: `gpucc-sm6225.c` was using
`CLK_ALPHA_PLL_TYPE_DEFAULT` for both PLLs, but SM6225/Khaje has different PLL
hardware:
- **PLL0** = **ZONDA PLL** (l=0x21, alpha=0x5555, config_ctl=0x08200800)
- **PLL1** = **LUCID PLL** (l=0x23, alpha=0xF000, config_ctl=0x20485699)

Using DEFAULT caused config writes to go to wrong register offsets (ZONDA has
CONFIG_CTL at 0x10, DEFAULT has USER_CTL at 0x10), so the PLL never locked.

### Fix (in `drivers/clk/qcom/gpucc-sm6225.c`)
- PLL0: `CLK_ALPHA_PLL_TYPE_ZONDA` + `clk_alpha_pll_zonda_ops` +
  `clk_zonda_pll_configure()` + `clk_alpha_pll_postdiv_ro_ops`
- PLL1: `CLK_ALPHA_PLL_TYPE_LUCID` + `clk_alpha_pll_lucid_ops` +
  `clk_lucid_pll_configure()` + `clk_alpha_pll_postdiv_lucid_ops`
- Postdividers, parent maps, freq table updated to match downstream Khaje
  (all gfx3d frequencies from PLL0_OUT_MAIN with div=1: 320/465/600/785/820/980)
- Probe: CX GDSC enabled + AHB (0x1078) + CXO (0x1060) clocks always-on
  before PLL config
- DTS: `power-domains = <&rpmpd SM6115_VDDCX>` added to gpucc node
- GPU OPP table updated to match new frequencies

### Verified working
- `gpu_cc_pll0` = 640 MHz, `gpu_cc_pll1` = 690 MHz
- `gpu_cc_gx_gfx3d_clk` = 320 MHz
- DRM: `bound 5900000.gpu (ops a3xx_ops)`
- `/dev/dri/card0` + `/dev/dri/renderD128` present
- devfreq: `simple_ondemand`, 6 OPPs
- Firmware: `qcom/a630_sqe.fw` loaded
- IOMMU: adreno_smmu bound

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

### Known harmless messages
- `supply vdd/vddcx not found, using dummy regulator` — normal for SM6115
  (upstream sm6115.dtsi also has no regulator properties)
- `sync_state() pending due to 596a000.gmu` — GMU wrapper has no driver,
  clocks stay enabled (harmless)

### Remaining (minor)
- Display still via simplefb (not DRM/KMS) — GPU bound to DRM but display
  scanout not through GPU yet (DSI/panel bringup was reverted, see below)
- No `glmark2` benchmark run yet (no internet on phone to apt-get install)

### Archive
`archive/mainline-gpucc-zonda-lucid-20260813-2244/` — working build.

## 2026-08-13: DRM/DSI panel display works — 720x1650 + backlight

The DSI/DRM panel bringup was completed. The missing piece was the MDSS CORE_BCR
reset in the `&mdss` node (`resets = <&dispcc DISP_CC_MDSS_CORE_BCR>;` in
`arch/arm64/boot/dts/qcom/sm6225.dtsi`). Without it the MDSS top-level interrupt
demux stayed in the state ABL left it in, so the DSI0 `VIDEO_DONE`/
`CMD_DMA_DONE` interrupt never reached the CPU and all panel DCS commands timed
out (`ret=-110`). With the reset the DSI interrupt fires, the 7nm PHY PLL
re-locks, and the panel initializes correctly.

Verified on slot `b`:
- `cat /sys/class/graphics/fb0/name` → `msmdrmfb`
- `cat /sys/class/drm/card0-DSI-1/modes` → `720x1650`
- `cat /sys/class/drm/card0-DSI-1/status` → `connected`
- `cat /sys/class/backlight/5e94000.dsi.0/brightness` → controllable
- Kernel console (fbcon) draws on the panel; systemd and ADB come up.

Final clean build (no temporary debug prints) archived at
`archive/mainline-dsi-final-20260813-1622/` (`boot-linux.img`, `Image.gz`,
`sm6225-xiaomi-fog.dtb`, `.config`, `pivot.cpio.gz`, `pack-ubuntu-boot.sh`,
plus the `linux_rootfs-24.04-new.sparse.img` for `cust`).

To flash this final image:
```sh
fastboot flash boot_b archive/mainline-dsi-final-20260813-1622/boot-linux.img
fastboot --set-active=b
fastboot reboot
```

The debug `pr_info` prints in `drivers/gpu/drm/msm/msm_mdss.c` and
`drivers/gpu/drm/msm/dsi/dsi_host.c` have been removed. Display still works and
`dmesg` is clean of the old `HW_INTR_STATUS`/`dsi_host_irq` spam.

### Key display deltas
- `arch/arm64/boot/dts/qcom/sm6225.dtsi` — `&mdss` now has:
  - `power-domains = <&dispcc MDSS_GDSC>;`
  - `resets = <&dispcc DISP_CC_MDSS_CORE_BCR>;`
- `arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dts` — display nodes and
  `xinli,ft8006s` panel enabled.
- `drivers/clk/qcom/dispcc-sm6375.c` — `disp_cc_sm6225_desc` exposes the MDSS
  GDSC and the MDSS/RSCC BCR resets.
- `drivers/gpu/drm/msm/dsi/phy/dsi_phy.c` — added `qcom,sm6225-dsi-phy-7nm`
  compat (same 7nm V4.1 config as sm6375).
- `drivers/gpu/drm/panel/panel-xinli-ft8006s.c` + Kconfig/Makefile — panel
  driver.

## 2026-08-13: FULL BOOT SUCCESS — mainline 7.1.5 + Ubuntu 24.04 via switch_root

After the display revert and the three `pivot-init` fixes below (fbcon,
`exec switch_root`, `/lib/systemd` symlink in the rootfs), the device now
boots **completely**: kernel console visible on screen, `pivot-init` mounts
`cust` (`/dev/sda8`), `switch_root`s into the Ubuntu 24.04 rootfs, systemd
comes up, and `adb` (over the rootfs's own USB gadget/adbd, not the initramfs
one) connects with hostname `rain-ubuntu`:

```
$ adb devices
rain    device
$ adb shell uname -a
Linux rain-ubuntu 7.1.5-dirty #108 SMP PREEMPT ... aarch64 GNU/Linux
$ adb shell cat /etc/os-release
PRETTY_NAME="Ubuntu 24.04 LTS"
```

Verified working end-to-end on slot `b` with:
- Kernel: `out/linux-7.1.5` built from current `mainline/linux-7.1.y` tree
  (display disabled at DT level, `CONFIG_FRAMEBUFFER_CONSOLE=y`).
- Boot image: `out/ubuntu-dualboot/boot-linux.img`, packed by
  `tools/pack-ubuntu-boot.sh` (offsets: `kernel_offset 0x8000`,
  `ramdisk_offset 0x1000000`, `dtb_offset 0x1f00000` — unchanged from the
  original working script, do not "fix" these, see the offset dead-end note
  below).
- `cust`: `linux_rootfs-24.04-new.sparse.img` (with the `/lib/systemd`
  symlink fix, see below) flashed via `fastboot flash cust`.

**This is now the reference-working combination.** Full snapshot archived at
`archive/mainline-ubuntu2404-FULLBOOT-20260813-1537/` (boot-linux.img,
Image.gz, dtb, kernel.config, pivot-init, pivot-ramdisk.cpio.gz,
pack-ubuntu-boot.sh, and the fixed `linux_rootfs-24.04-new.sparse.img` for
`cust`, plus `SHA256SUMS`/`VERIFIED.txt`). **Treat this as the baseline** for
any further work (Wi-Fi, DSI/panel, audio, etc.) — if a future change
regresses the boot, reflash `boot_b` and `cust` from this exact archive to
get back here:
```sh
fastboot flash boot_b archive/mainline-ubuntu2404-FULLBOOT-20260813-1537/boot-linux.img
fastboot flash cust archive/mainline-ubuntu2404-FULLBOOT-20260813-1537/linux_rootfs-24.04-new.sparse.img
fastboot --set-active=b && fastboot reboot
```
(`archive/mainline-simplefb-rollback-20260813-1122/` was an earlier,
pre-`pivot-init`-fix snapshot from the same session — superseded by the one above.)

### Dead-end investigated and reverted: boot.img load offsets
While debugging the "stuck on Mi logo forever" symptom, `ramdisk_offset`/
`dtb_offset` in `tools/pack-ubuntu-boot.sh` were temporarily bumped (from
`0x1000000`/`0x1f00000` to `0x4000000`/`0x5000000`) on the theory that the
~46 MB decompressed kernel `Image` overlapped the ramdisk region. This was a
red herring — an older working build (`out/boot-linux-final.img`) used the
same original small offsets with a similarly large kernel and booted fine.
**The offsets were reverted back to the original values** in
`tools/pack-ubuntu-boot.sh`; do not change them again without solid evidence.
The real causes of the "stuck on logo" symptom were (a) `CONFIG_FRAMEBUFFER_CONSOLE`
not being set (see below) and (b) a stale/"fake" fastboot session needing a
plain `fastboot reboot` before retrying (see the fastboot quirk note below) —
neither is related to boot.img load addresses.

## 2026-08-13: DRM/DSI display bringup reverted (caused boot hangs)

The DRM/DSI panel bringup (dispcc/mdss/mdss_dsi0/panel enabled, plus C
workarounds for SMMU/video-done/fbdev-unblank) was **reverted** because it
caused boot failures. Kernel/DTS are back to the last known-stable
**simplefb-only** display path (`STATUS.md` 2026-08-11: "simplefb display —
works — OK").

Reverted in `mainline/linux-7.1.y` (via `git checkout --`, these are tracked
files so the originals came back exactly):
- `drivers/gpu/drm/msm/dsi/dsi_host.c` (removed the `dsi_wait4video_done`
  busy-wait hack, restored the real VIDEO_DONE IRQ wait)
- `drivers/gpu/drm/msm/disp/dpu1/dpu_kms.c` (removed `dpu_kms_stop_boot_scanout`,
  which was worked around SMMU faults on domain-swap)
- `drivers/gpu/drm/msm/msm_fbdev.c` (removed the delayed-work fb-unblank hack)
- `drivers/gpu/drm/msm/dsi/phy/dsi_phy.c` (removed the `sm6225-dsi-phy-7nm` compat entry)
- `drivers/gpu/drm/panel/Kconfig`, `drivers/gpu/drm/panel/Makefile` (removed
  the `DRM_PANEL_XINLI_FT8006S` registration)

Disabled at the DT level in `arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dts`
(untracked file, hand-edited): `&dispcc`, `&mdss`, `&mdss_dsi0`,
`&mdss_dsi0_phy` are now `status = "disabled"`; the `panel` node and
`&mdss_dsi0_out` override were removed entirely. The `simple-framebuffer`
`chosen`/`framebuffer0` node (ABL splash handoff) was left untouched — that's
what should still show text/console on screen.

The panel driver source (`drivers/gpu/drm/panel/panel-xinli-ft8006s.c`,
untracked) was **not deleted**, just archived to
`archive/display-bringup-disabled-20260813/panel-xinli-ft8006s.c` and
unregistered from Kconfig/Makefile, so the DSI panel work can be resumed
later without re-writing it from scratch.

`drivers/pinctrl/qcom/pinctrl-khaje.c` and the `pinctrl-msm.c`/`Kconfig.msm`/
`Makefile` changes for it were **kept** — that's the base TLMM/pinctrl driver
for the whole SoC (needed for touch, wifi, etc.), not display-specific, and
isn't implicated in the boot hangs.

`.config` in `out/linux-7.1.5` already had `CONFIG_DRM_MSM=y` (built-in, not
a module — contrary to what an earlier note in this file said) and
`CONFIG_DRM_PANEL_XINLI_FT8006S` was already **not set**. `CONFIG_FB_SIMPLE=y`
is enabled. No `.config` changes were needed for this revert; the DT-level
`status = "disabled"` is what keeps `dpu_kms`/`dsi_host` from ever probing.

**Rebuilt and repacked after the revert** (see "Versioned build archives" below):
`archive/mainline-simplefb-rollback-20260813-1122/` — this is the current
candidate "working" build (simplefb only, no DSI/panel). It has **not yet
been flash-tested on the device** as of this writing — flash to `boot_b` and
confirm the screen still shows `pivot-init` output before trusting it as the
new safe fallback for slot `b`.

### `pivot-init` bugs fixed 2026-08-13 (found via live serial debugging over ttyGS0/ttyACM0)
1. **Dead rescue-shell block**: there was a leftover debug block that
   unconditionally ran `exec $BB sh` right after the `mdev -s`/`sleep 4`
   UFS-enumeration wait — the real `cust`-mount/`switch_root` logic further
   down (incl. the `/dev/sda8` fallback) was dead code and never ran on any
   previous boot. Removed.
2. **`CONFIG_FRAMEBUFFER_CONSOLE` was not set** in `out/linux-7.1.5/.config`,
   so nothing (kernel log or pivot-init) was ever drawn on screen even when
   everything was working — looked like a permanent hang on the Mi logo.
   Re-enabled (`scripts/config --enable CONFIG_FRAMEBUFFER_CONSOLE
   CONFIG_FRAMEBUFFER_CONSOLE_DETECT_PRIMARY CONFIG_FRAMEBUFFER_CONSOLE_ROTATION`).
   Kernel `dmesg`/`printk` output now shows on the physical screen via the
   `simple-framebuffer` node regardless of what `pivot-init`'s own
   stdout/stderr is redirected to (it moves to `/dev/ttyGS0` once the USB
   ACM gadget comes up — use `/dev/kmsg` for any debug prints that must be
   visible on-screen after that point).
3. **`switch_root` must be `exec`'d directly by PID 1, never as a plain
   subshell command.** `busybox switch_root` checks `getpid() == 1` and
   silently prints its usage text (no real error) if that check fails — this
   looks exactly like an argument error but isn't one. Don't wrap it in
   `$(...)`, pipes, or a plain `cmd 2>file` without `exec`; always
   `exec $BB switch_root /newroot /sbin/init` directly.
4. **Root cause of the actual switch_root failure**: the Ubuntu 24.04 rootfs
   (`linux_rootfs-24.04-new.img`/`.sparse.img`, and hence `cust`) has `/lib`
   as a **real directory** (containing only `firmware/`, `modules/`, and a
   hand-added `ld-linux-aarch64.so.1` symlink) instead of Ubuntu's usual
   merged-usr `/lib -> usr/lib` symlink. `/sbin/init -> ../lib/systemd/systemd`
   therefore pointed at a path that doesn't exist (real systemd binary is
   only at `/usr/lib/systemd/systemd`), so the final `execve()` inside
   `switch_root` failed with `ENOENT` ("can't execute '/sbin/init': No such
   file or directory") — after switch_root had *already* deleted the
   initramfs's own files, leaving the shell in a half-dead, non-recoverable
   state (had to power-cycle the phone). **Fixed** by adding
   `/lib/systemd -> ../usr/lib/systemd` inside the rootfs image (mounted the
   raw `linux_rootfs-24.04-new.img` via loop, `ln -sfn ../usr/lib/systemd
   lib/systemd`), regenerated `linux_rootfs-24.04-new.sparse.img` with
   `img2simg`, and reflashed `cust` (`fastboot flash cust ...sparse.img`).
   If other `/lib/*` paths turn out to be missing similarly, the proper fix
   is converting `/lib`, `/bin`, `/sbin` into real merged-usr symlinks to
   `usr/lib`, `usr/bin`, `usr/sbin` — this rootfs was very likely debootstrapped
   without `--merged-usr` or had `/lib` reconstructed by hand at some point.

## Versioned build archives
After every kernel/ramdisk (re)build, copy the artifacts into
`archive/<description>-<timestamp>/` (boot-linux.img, Image.gz, the dtb,
`.config`, `pivot-init`, `pivot-ramdisk.cpio.gz`, `SHA256SUMS`) so it's easy
to `fastboot flash boot_b` back to a previous version if a new build regresses.
Current archives:
- `archive/display-bringup-disabled-20260813/` — just the orphaned panel
  driver source, kept for future reference.
- `archive/mainline-simplefb-rollback-20260813-1122/` — first full build
  after the display revert above (simplefb only). Candidate new baseline.

## What this is
This project is trying to bring up a mainline 7.1.y Linux kernel on the Redmi 10C (`rain`/`fog`, Qualcomm SM6225) with a working display + backlight.

## What currently works
- Kernel builds and boots from `boot_b` slot.
- `simple-framebuffer` (ABL splash handoff, `chosen`/`framebuffer0` in the
  DTS) is the current display path — confirmed working previously in
  `STATUS.md` (2026-08-11), before the DSI/panel bringup attempt that was
  just reverted (see above). Needs a fresh flash test to reconfirm after
  the revert.
- Backlight pinctrl (lcm1p8en/lcmblen) nodes still exist in
  `sm6225-xiaomi-fog.dts` but are now unreferenced (panel node removed);
  harmless leftover, safe to ignore or delete later.
- `cust` partition (UFS, `sda8`) contains the Ubuntu 24.04 rootfs image.
- Phone can be restored to Android slot `a` via `fastboot --set-active=a && fastboot reboot`.

## What does NOT work yet
- DSI/DRM panel display is disabled (reverted, see above) — only
  bootloader-handoff `simplefb` framebuffer is available, no full DRM/KMS.
- `cust` partition mount in `pivot-init` was the previous blocker; a
  leftover debug `exec sh` was found and removed (see above) — needs a
  fresh boot test to confirm `cust` now actually mounts.

## Important file locations

| File | Purpose |
|------|---------|
| `/home/damir00109/Desktop/Redmi10c/mainline/linux-7.1.y` | mainline kernel source |
| `/home/damir00109/Desktop/Redmi10c/out/linux-7.1.5` | kernel build output (`Image.gz`, `dtbs`, `.config`) |
| `/home/damir00109/Desktop/Redmi10c/out/ubuntu-dualboot/pivot-init` | initramfs `init` script (cust mount, USB gadget, switch_root) |
| `/home/damir00109/Desktop/Redmi10c/out/ubuntu-dualboot/pivot.cpio.gz` | packed initramfs |
| `/home/damir00109/Desktop/Redmi10c/out/ubuntu-dualboot/boot-linux.img` | final v2 boot image (kernel + ramdisk + dtb) |
| `/home/damir00109/Desktop/Redmi10c/out/ubuntu-dualboot/linux_rootfs-24.04-new.img` | Ubuntu rootfs for `cust` (raw ext4, 1.8 GB) |
| `/home/damir00109/Desktop/Redmi10c/out/ubuntu-dualboot/linux_rootfs-24.04-new.sparse.img` | sparse-flashable version (1.2 GB) |
| `/home/damir00109/Desktop/Redmi10c/tools/pack-ubuntu-boot.sh` | pack `boot-linux.img` |

## Kernel .config requirements
(Superseded 2026-08-13 — see revert note at top.) The current `out/linux-7.1.5/.config`
already has `CONFIG_DRM_MSM=y` (built-in) and `CONFIG_DRM_PANEL_XINLI_FT8006S`
not set, which is correct for the current simplefb-only DTS. No `.config`
edits are needed right now. If DSI/panel bringup is resumed later, re-enable
`CONFIG_DRM_PANEL_XINLI_FT8006S=y` and re-add the `&mdss`/`&mdss_dsi0`/panel
nodes from the archived DTS history (see `git log` in the main repo for the
`eaf0d2c`..`fe993b8` commit range, and
`archive/display-bringup-disabled-20260813/panel-xinli-ft8006s.c`).

## Source code changes currently applied (after 2026-08-13 revert)
- `drivers/pinctrl/qcom/pinctrl-khaje.c` — added gpio103/106 as GPIO-capable (kept, not display-specific).
- `arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dts` — removed fake `lm36923`;
  `lcm1p8en`/`lcmblen` pinctrl nodes still defined but now unreferenced
  (panel node removed); `&dispcc`/`&mdss`/`&mdss_dsi0`/`&mdss_dsi0_phy` set
  to `status = "disabled"`.
- `drivers/gpu/drm/panel/panel-xinli-ft8006s.c` — unregistered from the
  build (Kconfig/Makefile reverted); source kept only in
  `archive/display-bringup-disabled-20260813/`.

## Testing procedure
1. Build: `make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc) -C mainline/linux-7.1.y O=/home/damir00109/Desktop/Redmi10c/out/linux-7.1.5 Image.gz dtbs`
2. Pack: `tools/pack-ubuntu-boot.sh`
3. **Archive** the result into `archive/<desc>-<timestamp>/` (see "Versioned build archives" above) before flashing.
4. Flash: `fastboot flash boot_b out/ubuntu-dualboot/boot-linux.img && fastboot --set-active=b && fastboot reboot`
5. Expected: screen lights up (simplefb only now — no DRM/panel), `pivot-init` messages appear, then it mounts `cust` and `switch_root` to Ubuntu.

## Known `cust` issue
- In Android, `cust` is `/dev/block/sda8`.
- `pivot-init` has a hard fallback to `/dev/sda8`; make sure `mdev -s` and a 3-5 second wait happen before the fallback is used.
- A leftover unconditional `exec sh` "rescue" block was found right before
  the `cust` search loop and removed on 2026-08-13 — previously this made
  the mount/switch_root code unreachable on every boot. If `cust` still
  isn't found after this fix, re-flash it from Android/TWRP with
  `dd if=/path/to/linux_rootfs-24.04-new.img of=/dev/block/sda8 bs=4M`.

## fastboot quirk: "Failed to load/authenticate boot image: Load Error"
If `fastboot boot`/`fastboot flash` returns
`FAILED (remote: 'Failed to load/authenticate boot image: Load Error')`,
this means the phone is **not actually in full fastboot** — it's sitting on
the boot logo from a previous `fastboot boot` attempt (a "fake"/degraded
fastboot enumeration), not real bootloader fastboot mode. `fastboot devices`
still lists it, but commands fail. Fix: run `fastboot reboot` first (which
kicks it back to a real bootloader fastboot session), wait for `fastboot
devices` to show it again, then retry the `boot`/`flash` command.

## Debugging pivot-init live (no display needed)
Once the USB ACM gadget comes up, `pivot-init` redirects its own
stdin/stdout/stderr to `/dev/ttyGS0`. On the host this shows up as
`/dev/ttyACM0` (check `lsusb` for `1d6b:0104 ... Composite Gadget`). Attach
with `sudo screen /dev/ttyACM0 115200` (or `stty -F /dev/ttyACM0 115200 raw
-echo && cat /dev/ttyACM0`) to see/interact with the rescue shell if
`pivot-init` falls back to one. Kernel `dmesg`/`printk` always also goes to
the physical screen (fbcon) regardless of this redirect.

**Careful**: if you manually retry `switch_root` from this rescue shell and
it fails again, its recursive-delete-old-root step may have already run,
deleting the initramfs's own busybox binary — the shell becomes a
non-recoverable zombie (no error, just stops responding to any new command).
The only way out at that point is a **manual power-cycle** (hold power ~10-15s,
then Vol-Down+Power for fastboot).

## Recovery
If the phone hangs or the new `boot_b` is broken:
```sh
fastboot --set-active=a
fastboot reboot
```
This returns to Android slot `a` (LineageOS).

## Last known good build
(Superseded 2026-08-13.) Candidate new baseline is
`archive/mainline-simplefb-rollback-20260813-1122/boot-linux.img` (simplefb
only, DSI/panel bringup reverted, `pivot-init` rescue-shell bug fixed) — see
the revert note at the top of this file. **Flash-test this before trusting
it**; nothing in this build has been booted on the device yet as of this
edit.
