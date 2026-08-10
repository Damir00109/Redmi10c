# Ubuntu 24.04 → 26.04 on rain (in-place)

Only after WiFi works and the phone is stable for days.

1. `sudo qcom-wifi-start.sh && sudo qcom-wifi-connect.sh`
2. Keep rain masks: never unmask tqftpserv/rmtfs/pd-mapper/NetworkManager until ready.
3. `sudo apt update && sudo apt full-upgrade`
4. Install `update-manager-core` if needed, then:
   `sudo do-release-upgrade` (or set Prompt=normal / follow resolute when offered)
5. After upgrade, re-apply rain overlay:
   - copy `tools/rain-overlay/` onto the rootfs (or re-flash cust built from updated tree)
   - confirm `usb-acm-gadget.service` is Type=oneshot (no --loop)
   - confirm modem units stay masked; WiFi only via qcom-wifi-*.sh
6. Reboot; check `/var/log/rain/latest.log`

Do **not** flash a raw 26.04 cloud image again — it enables tqftp/rmtfs/snapd at boot and RCU-stalls.
