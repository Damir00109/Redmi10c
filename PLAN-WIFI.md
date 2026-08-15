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

### ROOT CAUSE ANALYSIS (2026-08-15) — FIRMWARE MISMATCH

**This is the primary cause of soft-hangs, not NAPI/RX path!**

Comparison with working RB1/RB2 (QCM2290/SM6115, same SoC family):

| Parameter | Our SM6225 | RB1/RB2 (working) |
|--|--|--|
| soc_id | 0x40690000 | 0x40670000 |
| wlanmdsp.mbn version | WLAN.HL.3.2.4-01022 (2022-02-21) | WLAN.HL.3.3.7.c2-00931 (2023-10-14) |
| htt-ver | 3.96 | 3.114 |
| firmware-5.bin | crc d16e3444 (old features) | crc da9fbaf8 (new features) |

**Three firmware files, three problems:**

1. **wlanmdsp.mbn** — Our file (md5: 7584cbff) is from linux-firmware SDM845
   default, NOT from our phone. The phone's real firmware (md5: 2e4d09cc,
   from `/vendor/firmware_mnt/image/wlanmdsp.mbn`) has a different signature
   for SM6225/Khaje SoC. Same version string but different binary (byte 4601+).

2. **firmware-5.bin** — Our file (60 bytes, crc d16e3444) describes old
   firmware features. The qcm2290 version (crc da9fbaf8) has different
   feature bits including `SINGLE_CHAN_INFO_PER_CHANNEL` which prevents
   `chan info: invalid frequency 0 (idx 41 out of bounds)` warnings.

3. **board-2.bin** — Our file (867KB, md5 7f52ba16) may be from a different
   board. Should use phone's BDF from `/vendor/firmware_mnt/image/bd3qvdfu.bin`.

**Evidence from kernel bugzilla #217748:**
- Dmitry Baryshkov (Linaro) reports exact same symptoms on QCM2290/SM6115
- Fix: use SoC-specific wlanmdsp.mbn + matching firmware-5.bin
- linux-firmware now ships `ath10k/WCN3990/hw1.0/qcm2290/` subdirectory
- Driver patch (merged 6.9+): `firmware-name = "qcm2290"` DT property

**Our kernel 7.1.5 has this patch** — we just need the right firmware + DT property!

### Kernel patches applied (in `patches/redmi10c-mainline-20260813.patch`)
1. `snoc.c`: removed `netif_threaded_enable()` (threaded NAPI hangs between firmware-ver and htt-ver)
2. `mac.c`: don't advertise `SUPPORTS_PS` on WCN3990; force PS off when STA started
3. `htt_rx.c`: cap RX refill batch to 16 on WCN3990 (long `rx_ring.lock` hold soft-locks NAPI)

**NOTE:** Patches 1-3 may be unnecessary once firmware is correct. They mask
symptoms of firmware mismatch (old firmware doesn't handle PS/NAPI correctly).

### Userspace scripts (in rootfs `/usr/local/sbin/`)
- `qcom-wifi-start.sh` — modem PAS + tqftpserv + rmtfs + pd-mapper + ath10k_snoc → `wlan0`
- `qcom-wifi-connect.sh` — wpa_supplicant + udhcpc (no NM, no `iw`)
- `rain-wifi` — CLI wrapper (scan/status/connect via wpa_cli only)
- `qcom-wifi-nm.sh` — handoff wlan0 to NetworkManager after manual bring-up
- `rain-mmc-park` — park µSD IRQ to avoid sdhci_msm timeouts under wifi load

---

## Plan: 5 phases (REVISED)

### Phase 0: Fix firmware mismatch — CRITICAL (NEW)
**Goal:** Use correct SoC-specific firmware, eliminate `chan info` errors.

**Option A: Use phone's native firmware (safest, guaranteed signature match)**
1. Copy `/vendor/firmware_mnt/image/wlanmdsp.mbn` (md5: 2e4d09cc) to rootfs
   `/usr/lib/firmware/qcom/sm6225/wlanmdsp.mbn`
2. Copy `/vendor/firmware_mnt/image/bd3qvdfu.bin` as `board-2.bin`
3. Keep existing `firmware-5.bin` (matches version 3.2.4)
4. Test: boot, `qcom-wifi-start.sh`, check dmesg for `chan info` errors

**Option B: Try qcm2290 firmware from linux-firmware (newer, may work)**
1. Decompress `ath10k/WCN3990/hw1.0/qcm2290/wlanmdsp.mbn.zst`
2. Decompress `ath10k/WCN3990/hw1.0/qcm2290/firmware-5.bin.zst`
3. Add `firmware-name = "qcm2290"` to `&wifi` DT node
4. Place files in `/usr/lib/firmware/ath10k/WCN3990/hw1.0/qcm2290/`
5. Test: boot, check if modem DSP accepts signature (SigVerify error = reject)
6. If SigVerify fails → fall back to Option A

**Option C: Hybrid — phone's wlanmdsp.mbn + qcm2290 firmware-5.bin**
- May get `chan info` fix without signature issues
- Risky: feature mismatch between wlanmdsp 3.2.4 and firmware-5 3.3.7

**Recommended: try Option A first (native firmware), then B if stable.**

### Phase 1: Stabilize ath10k_snoc RX path (kernel) — IF NEEDED
**Goal:** eliminate soft-hang under traffic, achieve >5 min stable ping flood.

**Only needed if Phase 0 doesn't fully fix hangs!**

1. **Investigate CE IRQ coalescing** — WCN3990 CE IRQs fire too
   frequently under RX load. Add CE IRQ coalescing or batch processing.
   - File: `drivers/net/wireless/ath/ath10k/snoc.c` + `ce.c`

2. **Re-enable threaded NAPI** — upstream patch says threaded NAPI
   improves WCN3990 throughput by 15-25%. We disabled it as workaround.
   With correct firmware, it may work. Test carefully.

3. **Re-enable PS** — we disabled SUPPORTS_PS. With correct firmware,
   802.11 power save may work. Test with `iw dev wlan0 set power_save on`.

4. **Test:** `ping -f 1.1.1.1` for 5 min, then `iperf3 -c HOST -t 60`.

### Phase 2: Fix UFS/mmc interference (kernel + userspace) — HIGH
**Goal:** eliminate UFS timeouts and µSD IRQ storms during wifi load.

1. **UFS hibern8 disable** — set `ufs-qcom` to never enter hibern8 during
   wlan0 up. Already partially done via sysfs, make permanent in DTS.

2. **µSD park on boot** — `rain-mmc-park off` at boot via systemd.

3. **SMMU context bank quirks** — WCN3990 SMMU stream 0x1a0 may need
   `qcom,bypass-quirk` or CB-level `STALL` disabled.

4. **Test:** wifi + µSD mounted simultaneously, `dd if=/dev/mmcblk0` while
   `ping -f`.

### Phase 3: NetworkManager integration (userspace) — MEDIUM
**Goal:** `nmcli device wifi connect SSID password PASS` works after boot.

1. **Create `rain-wifi-bringup.service`** (systemd) that:
   - Runs `qcom-wifi-start.sh` (modem + ath10k + wlan0 up)
   - Waits for `wlan0` to appear (max 60s)
   - Sets `power_save off`
   - Exits 0 → hands off to NetworkManager

2. **NetworkManager config** `/etc/NetworkManager/conf.d/10-rain-wifi.conf`:
   ```ini
   [device-wlan0]
   device=wlans0
   managed=true
   
   [wifi-powersave]
   wifi.powersave=2  # disable (NM_WifiPowersaveDisable)
   ```

3. **udev rule** `/etc/udev/rules.d/99-rain-wifi.rules`:
   ```
   SUBSYSTEM=="net", ACTION=="add", KERNEL=="wlan0", \
     RUN+="/usr/local/sbin/rain-wifi-udev.sh"
   ```

4. **NM dispatcher script** `/etc/NetworkManager/dispatcher.d/10-rain`:
   - On `up`: ensure `power_save off`, set OEM MAC if changed
   - On `down`: don't tear down modem (keep PAS running for fast reconnect)

5. **Test:**
   ```bash
   sudo systemctl start rain-wifi-bringup
   sudo systemctl start NetworkManager
   nmcli device wifi connect 'SSID' password 'PASS'
   ```

### Phase 4: Boot-time auto-connect (userspace) — LOW
**Goal:** phone boots → wifi connects automatically to last network.

1. **NM connection profile** with `autoconnect=true` (after Phase 3 verified)

2. **Boot order:**
   ```
   rain-wifi-bringup.service (modem+ath10k+wlan0)
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
| 0A (native fw) | May still have old fw bugs | Then try Phase 0B |
| 0B (qcm2290 fw) | SigVerify reject by modem DSP | Fall back to 0A |
| 1 (kernel RX) | May need deeper CE/WMI rework | Start with coalescing + refill tuning |
| 2 (UFS/mmc) | DTS change may break boot | Test with `fastboot boot` first |
| 3 (NM) | NM may race with PAS | Use `After=rain-wifi-bringup.service` |
| 4 (auto) | Autoconnect may hang on bad signal | Add `connection.auth-timeout=20` |

## Estimated effort

| Phase | Complexity | Time |
|--|--|--|
| 0 (firmware) | Low (copy files + test) | 0.5 session |
| 1 (kernel RX) | High (kernel) | 2-3 sessions (if needed at all) |
| 2 (UFS/mmc) | Medium | 1 session |
| 3 (NM) | Low-Medium | 1 session |
| 4 (auto) | Low | 0.5 session |

## Success criteria

- [ ] No `chan info: invalid frequency` in dmesg
- [ ] `ping -f 1.1.1.1` for 5 min without hang
- [ ] `iperf3 -t 60` completes without reboot
- [ ] `nmcli device wifi connect` works after boot
- [ ] Reboot → auto-connect within 90s
- [ ] µSD + wifi simultaneously without hang

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
