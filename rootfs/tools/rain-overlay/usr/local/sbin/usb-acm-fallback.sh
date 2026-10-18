#!/bin/bash
# rain-usb-fallback — if nothing is bound to the UDC after boot, the ADB
# (FunctionFS) gadget failed to come up; bind a plain ACM gadget instead so
# the debug console over USB survives.  Logs the USB state for diagnosis.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

LOG=/var/log/rain-usb-fallback.log
G=/sys/kernel/config/usb_gadget/g1
FFS=/dev/usb-ffs/adb

log() { echo "$(date -Is) $*" | tee -a "$LOG"; }

sleep 20

UDC=$(ls /sys/class/udc 2>/dev/null | head -1)
[ -n "$UDC" ] || { log "no UDC, nothing to do"; exit 0; }

bound=$(cat "$G/UDC" 2>/dev/null | tr -d '\n')
if [ -n "$bound" ]; then
  log "UDC already bound to $bound — ok"
  exit 0
fi

log "UDC unbound — binding ACM fallback (udc=$UDC)"

# Drop the ADB function, keep ACM only
rm -f "$G/configs/c.1/ffs.adb" 2>/dev/null
mkdir -p "$G/functions/acm.usb0" 2>/dev/null
[ -e "$G/configs/c.1/acm.usb0" ] || \
  ln -sfn "$G/functions/acm.usb0" "$G/configs/c.1/acm.usb0" 2>/dev/null
echo 0x1d6b > "$G/idVendor" 2>/dev/null
echo 0x0104 > "$G/idProduct" 2>/dev/null

if echo "$UDC" > "$G/UDC" 2>/dev/null; then
  log "ACM fallback bound to $UDC"
else
  log "ACM fallback bind FAILED"
fi

{
  echo "--- state ---"
  echo "udc list: $(ls /sys/class/udc | tr '\n' ' ')"
  echo "gadget UDC: $(cat "$G/UDC" 2>/dev/null)"
  echo "ffs mounted: $(mountpoint -q "$FFS" && echo yes || echo no)"
  echo "ffs eps: $(ls "$FFS" 2>/dev/null | tr '\n' ' ')"
  echo "adbd: $(systemctl is-active adbd.service 2>/dev/null)"
  dmesg | grep -iE 'dwc3|udc|gadget|ep0|hsphy|ffs' | tail -30
} >> "$LOG" 2>&1
exit 0
