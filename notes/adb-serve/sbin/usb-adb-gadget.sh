#!/bin/bash
# USB ADB (FunctionFS) gadget for rain/fog.
# IMPORTANT: do NOT poke USB PHY MMIO in a loop — that hard-hangs SM6225.
# Bind UDC only after FunctionFS is mounted; adbd opens endpoints separately.
set +e
export PATH=/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/usb-adb-gadget.log
G=/sys/kernel/config/usb_gadget/g1
FFS=/dev/usb-ffs/adb
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

ffs_ready() {
  # ep0 appears after mount; ep1/ep2 after adbd writes descriptors
  [ -e "$FFS/ep0" ]
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

  # Already ADB-bound — leave alone
  if [ -d "$G" ] && ffs_ready; then
    cur=$(cat "$G/UDC" 2>/dev/null | tr -d '\n')
    idp=$(cat "$G/idProduct" 2>/dev/null | tr -d '\n')
    if [ -n "$cur" ] && [ "$idp" = "0x4ee7" ]; then
      log "already ok udc=$cur adb"
      return 0
    fi
  fi

  # Tear down prior ACM (or unbound) config carefully — oneshot only
  if [ -d "$G" ]; then
    cur=$(cat "$G/UDC" 2>/dev/null | tr -d '\n')
    if [ -n "$cur" ]; then
      echo "" >"$G/UDC" 2>/dev/null
      sleep 0.5
    fi
    rm -f "$G/configs/c.1/acm.usb0" "$G/configs/c.1/rndis.usb0" "$G/configs/c.1/ffs.adb" 2>/dev/null
  else
    mkdir -p "$G/strings/0x409" "$G/configs/c.1/strings/0x409"
  fi

  # Google ADB IDs so host adb picks the device up
  echo 0x18d1 >"$G/idVendor"
  echo 0x4ee7 >"$G/idProduct"
  echo rain >"$G/strings/0x409/serialnumber"
  echo Xiaomi >"$G/strings/0x409/manufacturer"
  echo rain-ubuntu >"$G/strings/0x409/product"
  echo "ADB+RNDIS+ACM" >"$G/configs/c.1/strings/0x409/configuration"
  echo 0xA0 >"$G/configs/c.1/bmAttributes"
  echo 500 >"$G/configs/c.1/MaxPower" 2>/dev/null || true

  mkdir -p "$G/functions/ffs.adb" 2>/dev/null
  mkdir -p "$G/functions/rndis.usb0" 2>/dev/null
  mkdir -p "$G/functions/acm.usb0" 2>/dev/null

  # stable MACs for usb0; host can set 02:00:00:00:00:02
  if [ ! -s "$G/functions/rndis.usb0/dev_addr" ]; then
    echo 02:00:00:00:00:01 >"$G/functions/rndis.usb0/dev_addr"
    echo 02:00:00:00:00:02 >"$G/functions/rndis.usb0/host_addr"
  fi

  mkdir -p "$FFS"
  if ! mountpoint -q "$FFS"; then
    mount -t functionfs adb "$FFS" 2>>"$LOG" || {
      log "functionfs mount failed"
      return 1
    }
  fi

  [ -e "$G/configs/c.1/ffs.adb" ] || ln -sfn "$G/functions/ffs.adb" "$G/configs/c.1/ffs.adb"
  [ -e "$G/configs/c.1/rndis.usb0" ] || ln -sfn "$G/functions/rndis.usb0" "$G/configs/c.1/rndis.usb0"
  [ -e "$G/configs/c.1/acm.usb0" ] || ln -sfn "$G/functions/acm.usb0" "$G/configs/c.1/acm.usb0"

  # Signal readiness for adbd (ep0 must exist). UDC bind waits for adbd.service
  # via Requires/After — here we only prepare FFS + leave UDC unbound if no ep1 yet.
  if [ ! -e "$FFS/ep0" ]; then
    log "ffs mounted but no ep0"
    return 1
  fi

  # If adbd already wrote endpoints, bind now; else leave unbound for adbd.service ExecStartPost
  if [ -e "$FFS/ep1" ] || [ -e "$FFS/ep1out" ]; then
    usb_phy_once
    if [ -z "$(cat "$G/UDC" 2>/dev/null | tr -d '\n')" ]; then
      if ! echo "$UDC" >"$G/UDC" 2>>"$LOG"; then
        log "bind UDC=$UDC failed"
        return 1
      fi
    fi
    log "GADGET_OK udc=$UDC (bound)"
  else
    log "GADGET_FFS_READY (await adbd for UDC bind)"
  fi
  return 0
}

# Called from adbd.service after daemon starts
bind_udc() {
  UDC=$(ls /sys/class/udc 2>/dev/null | head -1)
  [ -n "$UDC" ] || { log "bind_udc: no UDC"; return 1; }
  [ -d "$G" ] || { log "bind_udc: no gadget"; return 1; }
  for _ in $(seq 1 50); do
    if [ -e "$FFS/ep1" ] || [ -e "$FFS/ep1out" ]; then
      break
    fi
    sleep 0.1
  done
  cur=$(cat "$G/UDC" 2>/dev/null | tr -d '\n')
  if [ -n "$cur" ]; then
    log "bind_udc: already $cur"
    return 0
  fi
  usb_phy_once
  if ! echo "$UDC" >"$G/UDC" 2>>"$LOG"; then
    log "bind_udc failed UDC=$UDC"
    return 1
  fi
  log "bind_udc OK udc=$UDC"
  return 0
}

case "${1:-setup}" in
  bind) bind_udc; exit $? ;;
  setup|"") ;;
  *) echo "usage: $0 [setup|bind]" >&2; exit 2 ;;
esac

for i in $(seq 1 20); do
  setup_once && exit 0
  sleep 1
done
exit 1
