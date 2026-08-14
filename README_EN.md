# Redmi 10C (rain/fog) — mainline Linux bring-up

A project to port **mainline Linux (7.1.y)** to the Xiaomi Redmi 10C
(codename `rain`, variant `fog`, SoC Qualcomm SM6225 "Khaje") with boot into
Ubuntu 24.04 via a custom `initramfs` (`pivot-init` → `switch_root`),
without replacing Android — the free A/B slot `b` and the `cust` partition
are used for the rootfs.

**Current status:** the kernel boots fully, `switch_root` into Ubuntu 24.04
succeeds (systemd, adb over USB gadget). The display works through DRM/DSI:
FT8006S panel at 720x1650, backlight, fbcon console.
GPU Adreno 610: PLL/clock/DRM probe work, vulkaninfo detects the GPU,
headless 3D render works, but the display path (DPU scanout from GPU buffer)
causes a hardware lockup. See [`STATUS.md`](STATUS.md) for details.

Boot time: **27.5 seconds** (kernel 9.8s + userspace 17.7s).

This repository contains **source code and patches**, not prebuilt binaries.
Below is a full guide on how to build everything from scratch.

[Русская версия](README.md)

---

## Repository structure

| Path | Purpose |
|---|---|
| `patches/redmi10c-mainline-*.patch` | Patch on top of `git.kernel.org/.../linux.git` (tag `v7.1.5`) with all Redmi 10C modifications |
| `configs/rain-fog-working.config` | Working kernel `.config` (fully boot-tested in Ubuntu) |
| `configs/busybox-1.36.1.config` | `.config` for busybox used in `pivot-init` |
| `tools/pivot-init` | Initramfs init script: sets up USB ACM, mounts `cust`, runs `switch_root` |
| `tools/pack-ubuntu-boot.sh` | Builds `boot-linux.img` (kernel + initramfs + dtb) |
| `tools/build-ubuntu-dualboot.sh` | Builds Ubuntu rootfs (`linux_rootfs*.img`) via debootstrap |
| `tools/restore-android.sh` | Restore slot `a` to stock Android/LineageOS |
| `STATUS.md`, `PLAN.md`, `HARDWARE-DRIVERS.md` | Porting history by subsystem |
| `notes/` | Research notes: dmesg, DT dumps, debug logs from the live device |

---

## Component status

| Component | Status | Description |
|-----------|--------|----------|
| Boot / kernel | ✅ | mainline 7.1.5, slot B, Ubuntu 24.04 |
| CPU / SMP | ✅ | 8 cores, PREEMPT |
| UFS | ✅ | sda1-sda16, cust=sda8 |
| Display (DSI/DRM) | ✅ | 720x1650, Xinli FT8006S, DPU + DSI 7nm PHY |
| Backlight | ✅ | sysfs |
| Touchscreen | ✅ | FTS SPI, focaltech driver |
| Power key / Volume | ✅ | pm8941_pwrkey / pm8941_resin |
| Vibrator | ✅ | gpio-vibrator |
| Charger (smb1351) | ✅ | I2C, polling, JEITA |
| Fuel gauge (sh366101) | ✅ | Battery %, temp |
| microSD | ✅ | |
| Type-C CC (wusb3801) | ✅ | /sys/class/typec/port0 |
| USB RNDIS | ✅ | usb0=10.0.0.2/24 |
| ACM serial / ADB | ✅ | ttyACM0, usb-adb-gadget |
| Thermal (pm6125) | ✅ | ~38°C |
| Sensors (lm-sensors) | ✅ | Temperatures |
| GPU Adreno 610 | ⚠️ 70% | Headless render ✅, DPU scanout crash |
| Vulkan Turnip | ⚠️ 70% | vulkaninfo ✅, vkcube swapchain crash |
| Wi-Fi | ❌ | No modem firmware |
| Modem | ❌ | remoteproc offline |
| Audio | ❌ | Not configured |
| Bluetooth | ❌ | WCN3990, not started |
| GPS | ❌ | Not started |
| Camera | ❌ | Not started |
| Fingerprint | ❌ | FPC/Silead |
| NFC | ❌ | NQ-NCI, not started |
| Boot time | ✅ 27.5s | Kernel 9.8s + userspace 17.7s |

**Overall progress: ~70%** (user-facing functionality ~55%)

---

## Host requirements (Ubuntu/Debian)

```sh
sudo apt install \
  gcc-aarch64-linux-gnu g++-aarch64-linux-gnu binutils-aarch64-linux-gnu \
  device-tree-compiler cpio python3 git \
  android-sdk-libsparse-utils \
  adb fastboot
```

The cross-compiler must produce the `aarch64-linux-gnu-` prefix (tested with
GCC 15.2.0, but most modern versions should work).

---

## Step 1 — Kernel: clone, apply patch, build

```sh
# 1.1 Clone mainline linux-stable at the required tag
git clone --branch v7.1.5 --depth 1 \
  https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git \
  mainline/linux-7.1.y
cd mainline/linux-7.1.y

# 1.2 Apply the Redmi 10C patch (adds DTS, panel, pinctrl-khaje,
#     DSI/UFS/USB/ath10k fixes, etc.)
git apply --stat ../../patches/redmi10c-mainline-20260813.patch   # preview
git apply ../../patches/redmi10c-mainline-20260813.patch

# 1.3 Copy the working .config
mkdir -p ../../out/linux-7.1.5
cp ../../configs/rain-fog-working.config ../../out/linux-7.1.5/.config

# 1.4 Build
cd ../..
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" \
  -C mainline/linux-7.1.y O="$PWD/out/linux-7.1.5" \
  olddefconfig Image.gz dtbs
```

Result: `out/linux-7.1.5/arch/arm64/boot/Image.gz` and
`out/linux-7.1.5/arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dtb`.

---

## Step 2 — busybox for initramfs (static aarch64 build)

```sh
git clone --branch 1_36_1 https://github.com/mirror/busybox.git tools/busybox-src
cd tools/busybox-src
cp ../../configs/busybox-1.36.1.config .config
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j"$(nproc)" oldconfig busybox
mkdir -p ../../out/initramfs-root/bin
cp busybox ../../out/initramfs-root/bin/busybox
cd ../..
```

Verify the binary is static and aarch64:
`file out/initramfs-root/bin/busybox` → `ELF 64-bit ... ARM aarch64 ...
statically linked`.

---

## Step 3 — mkbootimg (AOSP boot image packing tool)

```sh
git clone https://android.googlesource.com/platform/system/tools/mkbootimg \
  tools/mkbootimg
```

(The guide below uses `tools/mkbootimg/mkbootimg.py` — the system
`/usr/bin/mkbootimg` from Ubuntu packages may not support the required
`--header_version 2` option.)

---

## Step 4 — Build boot-linux.img

```sh
bash tools/pack-ubuntu-boot.sh
```

The script:
1. Takes `out/initramfs-root/bin/busybox`, creates relative symlinks for
   all applets (`ls`, `mount`, `switch_root`, …).
2. Copies `tools/pivot-init` as `/init` in the initramfs.
3. Packs `pivot.cpio.gz`.
4. Uses `mkbootimg.py` (header v2) to build `out/ubuntu-dualboot/boot-linux.img`
   from `Image.gz` + `pivot.cpio.gz` + `sm6225-xiaomi-fog.dtb`.

The result should be ~17 MiB.

---

## Step 5 — Ubuntu rootfs for the `cust` partition

The full debootstrap pipeline is in `tools/build-ubuntu-dualboot.sh` (requires
`sudo`, `debootstrap`, `qemu-user-static` for aarch64). It creates
`out/ubuntu-dualboot/linux_rootfs.img` (ext4) and a sparse version for fastboot.

**Important:** the rootfs must have either a proper merged-usr (`/lib -> usr/lib`)
or at least a symlink `/lib/systemd -> ../usr/lib/systemd`, otherwise
`/sbin/init -> ../lib/systemd/systemd` won't resolve and `switch_root` will
fail with `ENOENT` AFTER the initramfs is already deleted. Verify after building:

```sh
sudo mount -o loop <linux_rootfs.img> /mnt
readlink -f /mnt/sbin/init   # must resolve to an existing file
sudo umount /mnt
```

---

## Step 6 — Flashing (slot `b` only, do not touch slot `a`!)

Slot `a` is kept as a working Android/LineageOS fallback with verification
(AVB) enabled. Slot `b` is for mainline testing, with a custom `vbmeta_b`
(verification disabled, `--flags 2` in `avbtool`) and no `dtbo_b` (a
populated `dtbo` caused boot hangs).

```sh
fastboot erase dtbo_b
fastboot flash boot_b out/ubuntu-dualboot/boot-linux.img
fastboot flash cust   out/ubuntu-dualboot/linux_rootfs.sparse.img
fastboot --set-active=b
fastboot reboot
```

Verification after boot (USB ACM/adb should come up automatically):

```sh
adb devices           # expect "rain    device"
adb shell uname -a     # 7.1.5-dirty ...
adb shell cat /etc/os-release   # Ubuntu 24.04 LTS
```

Rollback to Android (slot `a`):

```sh
fastboot --set-active=a
fastboot reboot
```

---

## What doesn't work yet

- **GPU 3D rendering + display path** — the main unsolved task.
  Headless GPU render works, but DPU scanout from GPU buffer → hardware lockup.
  SMMU context fault: SID=0x420, iova=0x5c000000.
- **Wi-Fi** — no modem firmware, remoteproc offline
- **Audio** — not configured
- **Bluetooth** — WCN3990, not started
- **Modem (voice/data)** — not started
- **Camera** — not started
- **GPS** — not started
- **Fingerprint** — proprietary FPC/Silead
- **NFC** — NQ-NCI, not started

---

## Safety

- **Do not disable verification (`dm-verity`/AVB) on slot `a`** — it is
  the only guaranteed working fallback.
- Destructive operations (erase/flash system partitions, change `vbmeta`)
  are only performed on slot `b` or explicitly designated test partitions
  (`cust`).
