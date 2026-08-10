# Android poweroff / charger notes (rain)

Captured: 2026-08-01 while on slot `_a` (Lineage restore).
Logs: `notes/android-poweroff-20260801-094945/`

## Device
- product: `rain` / 220333QNY
- build: `BP4A.251205.006 release-keys`
- slot: `_a`
- battery (dumpsys): **72%**, USB powered, status=2 (Charging), 4.15 V

## How Android turns off (this phone)
1. UI / `shutdown,userrequested` → init watches **`sys.powerctl`** → `reboot -p` / kernel halt.
2. Stock kernel **implements PSCI SYSTEM_OFF** (unlike our mainline: `failed to set PC mode: -3`).
3. With USB still attached, **XBL/ABL** may re-enter **off-mode charge** (animation) — that path is **bootloader**, not full Android userspace.
4. History prop shows prior successful user shutdown:
   `persist.sys.boot.reason.history` → `shutdown,userrequested` then later `bootloader`.

## Charger mode artifacts
- `/vendor/etc/charger_fstab.qti` — only mounts `persist` for charger context.
- No world-readable `/system/bin/charger` from unprivileged shell (need root / boot.img unpack).
- Offline charge UI is **not** something Ubuntu can “call”; without SYSTEM_OFF we only get reset → active slot boot.

## power_supply on Android
Sysfs nodes present: `ac`, `battery`, `bms`, `usb` (shell cannot read uevent without root; BatteryService has the numbers).

## Contrast with Ubuntu dual-boot
| | Android | Ubuntu mainline |
|--|---------|-----------------|
| PSCI SYSTEM_OFF | works | broken (−3) |
| FG / smb1351 | in DT, live | **disabled** (i2c hang risk) |
| Touch | present (FPC/Focal in Android DT) | **no** touch event node |
| “Power off” + USB | off → ABL charger | SoC reset → reboot Ubuntu |

## Return to Ubuntu
```bash
adb reboot bootloader
fastboot erase dtbo          # required
fastboot set_active b
fastboot flash boot_b out/ubuntu-dualboot/boot-linux.img
# cust already has rootfs; flash only if needed
fastboot reboot
```

## Next for Ubuntu UI
1. Touch bringup (Focaltech) before OSK.
2. Careful FG re-enable for %.
3. Soft-off already avoids systemd reboot; true offline-charge needs working SYSTEM_OFF or dedicated charger initramfs.
