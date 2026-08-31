#!/bin/bash
# USB ACM gadget for rain/fog.
# IMPORTANT: do NOT poke USB PHY MMIO in a loop — that hard-hangs SM6225.
# Optional one-shot PHY init only if RAIN_USB_PHY=1 (default: off in userspace;
# pivot-init already brought USB up once).
set +e
export PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/usb-acm-gadget.log
G=/sys/kernel/config/usb_gadget/g1
BB=/usr/local/bin/busybox
: "${RAIN_USB_PHY:=0}"

log() { echo "$(date -Is) $*" >>"$LOG"; }

rmw32() {
  a=$1; clr=$2; set=$3
  cur=$("$BB" devmem "$a" 32 2>/dev/null) || return 1
  cur=${cur#0x}
  nv=$(printf '0x%08x' $(( (0x$cur & ~clr) | set )))
  "$BB" devmem "$a" 32 "$nv" 2>/dev/null
}

usb_phy_once() {
  [ "$RAIN_USB_PHY" = 1 ] || return 0
  [ -x "$BB" ] || return 0
  log "phy once (RAIN_USB_PHY=1)"
  rmw32 0x04ef8810 0x0 0x10100000 >/dev/null
  rmw32 0x04ef8830 0x0 0x01000000 >/dev/null
}

setup_once() {
  modprobe libcomposite 2>/dev/null || true
  mkdir -p /sys/kernel/config 2>/dev/null
  mountpoint -q /sys/kernel/config || mount -t configfs configfs /sys/kernel/config 2>/dev/null

  UDC=$(ls /sys/class/udc 2>/dev/null | head -1)
  if [ -z "$UDC" ]; then
    log "no UDC yet"
    return 1
  fi

  # Already bound with tty — leave alone (never rebind / never MMIO)
  if [ -c /dev/ttyGS0 ] && [ -d "$G" ]; then
    cur=$(cat "$G/UDC" 2>/dev/null | tr -d '\n')
    if [ -n "$cur" ]; then
      log "already ok udc=$cur"
      return 0
    fi
  fi

  if [ ! -d "$G" ]; then
    mkdir -p "$G/strings/0x409" "$G/configs/c.1/strings/0x409"
    mkdir -p "$G/functions/acm.usb0"
    echo 0x1d6b >"$G/idVendor"
    echo 0x0104 >"$G/idProduct"
    echo rain >"$G/strings/0x409/serialnumber"
    echo Xiaomi >"$G/strings/0x409/manufacturer"
    echo rain-ubuntu >"$G/strings/0x409/product"
    echo ACM >"$G/configs/c.1/strings/0x409/configuration"
    echo 0xA0 >"$G/configs/c.1/bmAttributes"
  fi
  rm -f "$G/configs/c.1/rndis.usb0" 2>/dev/null
  [ -e "$G/configs/c.1/acm.usb0" ] || ln -sfn "$G/functions/acm.usb0" "$G/configs/c.1/acm.usb0"

  # Only unbind/rebind if needed — no PHY poke storm
  cur=$(cat "$G/UDC" 2>/dev/null | tr -d '\n')
  if [ -n "$cur" ] && [ ! -c /dev/ttyGS0 ]; then
    echo "" >"$G/UDC" 2>/dev/null
    sleep 0.5
  fi
  usb_phy_once
  if [ -z "$(cat "$G/UDC" 2>/dev/null | tr -d '\n')" ]; then
    if ! echo "$UDC" >"$G/UDC" 2>>"$LOG"; then
      log "bind UDC=$UDC failed"
      return 1
    fi
  fi
  for _ in 1 2 3 4 5 6 7 8 9 10; do
    [ -c /dev/ttyGS0 ] && break
    sleep 0.2
  done
  if [ -c /dev/ttyGS0 ]; then
    log "GADGET_OK udc=$UDC"
    return 0
  fi
  log "bound but no ttyGS0"
  return 1
}

# oneshot only — never --loop with PHY refresh
for i in $(seq 1 20); do
  setup_once && exit 0
  sleep 1
done
exit 1
