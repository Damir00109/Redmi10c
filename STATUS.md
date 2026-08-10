# Status — 2026-08-05 (evening)

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
| GPU Adreno | not started | 0% |
| Display (DRM/DSI) | not started | 0% |
| Bluetooth | not started | 0% |
| GPS | not started | 0% |
| Modem (voice/data) | not started | 0% |
| Camera | not started | 0% |
| Sensors | not started | 0% |

### Remaining blocks only

| Блок | Статус | % |
|--|--|--|
| Wi-Fi | assoc + ping OK, soft-hang ~60s | 60% |
| GPU Adreno 610 | DT есть, не проверено | 5% |
| Display (DRM/DSI + panel) | disabled в DT | 0% |
| Bluetooth | WCN3990 BTFM, не начато | 0% |
| Audio codec / speakers | не начато | 0% |
| Camera | не начато | 0% |
| Sensors | не найдены в DT/I2C | 0% |
| GPS | не начато | 0% |
| Modem (voice/data) | не начато | 0% |
| Fingerprint | проприетарный FPC/Silead | 0% |
| NFC | conf есть, не начато | 0% |

**Общий процент портирования (по блокам): ~60%** — но осталось самое сложное (Wi-Fi stability, GPU/Display, Audio, Modem/Camera). Реально пользовательская функциональность — скорее **40–45%**.


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
sudo rain-wifi connect '2.4GHz_WiFi_219' 'GP54006948'
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
