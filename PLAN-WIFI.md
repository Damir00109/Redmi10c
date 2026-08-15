# Wi-Fi Stack Stabilization Plan — rain/fog WCN3990

## Current state (2026-08-15)

### What works
- `ath10k_snoc` + modem PAS path: modem.mdt → tqftpserv → wlanmdsp.mbn → `wlan0`
- WPA2 association: `COMPLETED` to `2.4GHz_WiFi_219`
- DHCP: `192.168.1.38/24`
- Ping: `1.1.1.1` / `8.8.8.8` OK
- OEM MAC preserved: `f0:6c:5d:02:36:a2`

### What fails
- **Soft-hang after 30–60s** of connected traffic → ADB dies, often self-reboot
- Root cause class: RX/NAPI soft-lock under load (ath10k_snoc CE/WMI path)
- `iw` commands (raw nl80211) often soft-hang the board
- NetworkManager races with PAS bring-up → must be masked during boot

### Kernel patches applied (in `patches/redmi10c-mainline-20260813.patch`)
1. `snoc.c`: removed `netif_threaded_enable()` (threaded NAPI hangs between firmware-ver and htt-ver)
2. `mac.c`: don't advertise `SUPPORTS_PS` on WCN3990; force PS off when STA started
3. `htt_rx.c`: cap RX refill batch to 16 on WCN3990 (long `rx_ring.lock` hold soft-locks NAPI)

### Userspace scripts (in rootfs `/usr/local/sbin/`)
- `qcom-wifi-start.sh` — modem PAS + tqftpserv + rmtfs + pd-mapper + ath10k_snoc → `wlan0`
- `qcom-wifi-connect.sh` — wpa_supplicant + udhcpc (no NM, no `iw`)
- `rain-wifi` — CLI wrapper (scan/status/connect via wpa_cli only)
- `qcom-wifi-nm.sh` — handoff wlan0 to NetworkManager after manual bring-up
- `rain-mmc-park` — park µSD IRQ to avoid sdhci_msm timeouts under wifi load

---

## Plan: 4 phases

### Phase 1: Stabilize ath10k_snoc RX path (kernel) — CRITICAL
**Goal:** eliminate soft-hang under traffic, achieve >5 min stable ping flood.

1. **Investigate CE IRQ coalescing** — WCN3990 CE (Copy Engine) IRQs fire too
   frequently under RX load. Add `ath10k_snoc` CE IRQ coalescing or batch
   processing to reduce NAPI re-entry rate.
   - File: `drivers/net/wireless/ath/ath10k/snoc.c` + `ce.c`
   - Reference: downstream `qcacld` uses `ce_pipe->dest_ring->timer_interval`

2. **Disable threaded NAPI permanently for WCN3990** — already done, but also
   disable `NAPI_HAS_PENDING_BUSY_POLL` and set `gro_flush_timeout=0` on wlan0
   via udev script to prevent softirq starvation.

3. **Tune RX refill threshold** — current cap is 16. Try 8 or 4 to reduce
   single NAPI poll duration. Monitor with `ethtool -S wlan0` (if available)
   or ath10k debugfs (`/sys/kernel/debug/ath10k/qcom,wcn3990-wifi/`).

4. **Add watchdog timer** — if NAPI poll exceeds 50ms, dump CE state to dmesg
   and force NAPI break. This won't fix the hang but will give us diagnostics.

5. **Test:** `ping -f 1.1.1.1` for 5 min, then `iperf3 -c HOST -t 60`.

### Phase 2: Fix UFS/mmc interference (kernel + userspace) — HIGH
**Goal:** eliminate UFS timeouts and µSD IRQ storms during wifi load.

1. **UFS hibern8 disable** — set `ufs-qcom` to never enter hibern8 during
   wlan0 up. Already partially done via sysfs `power/control=on`, but make
   it permanent in DTS: `&ufshc { qcom,no-hibern8; }` or equivalent.

2. **µSD park on boot** — `rain-mmc-park off` runs at wifi-start, but should
   run at boot via systemd `Before=qcom-wifi-bringup.service`.

3. **SMMU context bank quirks** — WCN3990 SMMU stream 0x1a0 may need
   `qcom,bypass-quirk` or CB-level `STALL` disabled. Check downstream
   `qcom_smmu_context_fault` behavior vs mainline.

4. **Test:** wifi + µSD mounted simultaneously, `dd if=/dev/mmcblk0` while
   `ping -f`.

### Phase 3: NetworkManager integration (userspace) — MEDIUM
**Goal:** `nmcli device wifi connect SSID password PASS` works after boot,
no manual scripts needed.

1. **Create `rain-wifi-bringup.service`** (systemd) that:
   - Runs `qcom-wifi-start.sh` (modem + ath10k + wlan0 up)
   - Waits for `wlan0` to appear (max 60s)
   - Sets `power_save off` via `iw` (or skip if `iw` hangs — use debugfs)
   - Exits 0 → hands off to NetworkManager

2. **NetworkManager config** `/etc/NetworkManager/conf.d/10-rain-wifi.conf`:
   ```ini
   [device-wlan0]
   device=wlans0
   managed=true
   
   [connection-rain]
   # Disable autoconnect races — only connect after bringup service done
   connection.autoconnect=false
   
   [wifi-powersave]
   wifi.powersave=2  # disable (NM_WifiPowersaveDisable)
   ```
   - `wifi.powersave=2` = NM disables 802.11 PS (we already force it off in kernel)

3. **udev rule** `/etc/udev/rules.d/99-rain-wifi.rules`:
   ```
   SUBSYSTEM=="net", ACTION=="add", KERNEL=="wlan0", \
     RUN+="/usr/local/sbin/rain-wifi-udev.sh"
   ```
   - udev script: set `gro_flush_timeout=0`, `napi_defer_hard_irqs=0`,
     `power_save=off` (via debugfs if `iw` hangs)

4. **NM dispatcher script** `/etc/NetworkManager/dispatcher.d/10-rain`:
   - On `up`: ensure `power_save off`, set OEM MAC if changed
   - On `down`: don't tear down modem (keep PAS running for fast reconnect)

5. **Mask conflicting services:**
   - `systemctl mask qcom-wifi-bringup.service` (old one that races)
   - Keep `rain-wifi-bringup.service` as the only boot-time bringup

6. **Test:**
   ```bash
   sudo systemctl start rain-wifi-bringup
   sudo systemctl start NetworkManager
   nmcli device wifi connect 'SSID' password 'PASS'
   # Should work without manual scripts
   ```

### Phase 4: Boot-time auto-connect (userspace) — LOW
**Goal:** phone boots → wifi connects automatically to last network.

1. **NM connection profile** with `autoconnect=true` (after Phase 3 verified):
   ```ini
   [connection]
   id=rain-home
   type=wifi
   autoconnect=true
   
   [wifi]
   ssid=2.4GHz_WiFi_219
   
   [wifi-security]
   key-mgmt=wpa-psk
   psk=GP54006948
   ```

2. **Boot order:**
   ```
   rain-wifi-bringup.service (modem+ath10k+wlan0 up)
     → NetworkManager.service (autoconnect)
       → nm-dispatcher (power_save off, MAC fix)
   ```

3. **Test:** reboot phone, wait 90s, `ping` should work without SSH/ADB.

---

## Architecture diagram

```
Boot
  │
  ▼
rain-wifi-bringup.service
  ├─ rain-mmc-park off          (park µSD)
  ├─ qrtr-ns                    (QRTR nameserver)
  ├─ pd-mapper                  (PDR)
  ├─ rmtfs                      (remote filesystem)
  ├─ tqftpserv                  (TFTP for wlanmdsp.mbn)
  ├─ echo start > remoteproc0   (modem PAS)
  ├─ insmod ath10k_snoc.ko      (wifi driver)
  └─ ip link set wlan0 up
  │
  ▼ (wlan0 exists, modem running)
  │
NetworkManager.service
  ├─ nmcli device set wlan0 managed yes
  ├─ wifi.powersave=2 (disabled)
  ├─ autoconnect to saved SSID
  └─ DHCP via internal dhclient
  │
  ▼ (connected)
  │
nm-dispatcher.d/10-rain
  ├─ iw dev wlan0 set power_save off  (if iw works)
  └─ echo OEM MAC if changed
```

---

## Risk assessment

| Phase | Risk | Mitigation |
|--|--|--|
| 1 (kernel RX) | May need deeper CE/WMI rework | Start with coalescing + refill tuning |
| 2 (UFS/mmc) | DTS change may break boot | Test with `fastboot boot` first |
| 3 (NM) | NM may race with PAS | Use `After=rain-wifi-bringup.service` |
| 4 (auto) | Autoconnect may hang on bad signal | Add `connection.auth-timeout=20` |

## Estimated effort

| Phase | Complexity | Time |
|--|--|--|
| 1 | High (kernel) | 2-3 sessions |
| 2 | Medium | 1 session |
| 3 | Low-Medium | 1 session |
| 4 | Low | 0.5 session |

## Success criteria

- [ ] `ping -f 1.1.1.1` for 5 min without hang
- [ ] `iperf3 -t 60` completes without reboot
- [ ] `nmcli device wifi connect` works after boot
- [ ] Reboot → auto-connect within 90s
- [ ] µSD + wifi simultaneously without hang
