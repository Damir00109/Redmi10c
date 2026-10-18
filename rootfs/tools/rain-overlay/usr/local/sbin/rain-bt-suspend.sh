#!/bin/bash
# Work around hci_uart_qca's suspend callback: qca_suspend() times out
# (returns -110) and the kernel then aborts the *whole* system sleep
# ("Some devices failed to suspend").  Detach the BT serdev before the
# sleep and re-attach it afterwards, so the rest of the system can suspend;
# the driver power-cycles the controller on rebind, so BT comes back.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

DRV=/sys/bus/serial/drivers/hci_uart_qca
DEV=serial0-0
LOG=/var/log/rain-bt-suspend.log
log() { echo "$(date -Is) $*" >> "$LOG"; }

case "$1" in
  unbind)
    if [ ! -e "$DRV/$DEV" ]; then
      log "unbind: $DEV not bound"
      exit 0
    fi
    if echo "$DEV" > "$DRV/unbind"; then
      log "unbound $DEV (hci_uart_qca would have aborted the sleep)"
    else
      log "unbind FAILED"
      exit 1
    fi
    ;;
  rebind)
    if [ -e "$DRV/$DEV" ]; then
      log "rebind: already bound"
      exit 0
    fi
    if echo "$DEV" > "$DRV/bind"; then
      log "bound $DEV"
      sleep 3
      log "hci0: $(hciconfig hci0 2>/dev/null | sed -n '3p')"
    else
      log "rebind FAILED"
      exit 1
    fi
    ;;
  *)
    echo "usage: $0 [unbind|rebind]" >&2
    exit 2
    ;;
esac
