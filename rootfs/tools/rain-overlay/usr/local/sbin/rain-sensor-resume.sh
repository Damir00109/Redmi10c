#!/bin/bash
# Re-attach the Snapdragon Sensor Core services after a resume.
#
# The ADSP faults on resume (mainline never notifies the DSP about a system
# sleep), remoteproc restarts it, and hexagonrpcd's fastrpc connection dies
# with "Could not fetch next FastRPC message: Broken pipe" — both services
# then deactivate and the sensors stay dead until a reboot.  Restarting them
# re-attaches to the fresh ADSP instance.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

LOG=/var/log/rain-resume-probe.log
log() { echo "$(date -Is) $*" >> "$LOG"; }

# Resolve the ADSP by name: the remoteproc index depends on probe order.
ADSP=
for d in /sys/class/remoteproc/remoteproc*; do
  [ -r "$d/name" ] || continue
  [ "$(cat "$d/name")" = adsp ] && { ADSP=$d; break; }
done
[ -n "$ADSP" ] || { log "sensor resume: no adsp remoteproc"; exit 1; }

# Wait for the ADSP and the fastrpc device to come back.
for _ in $(seq 1 20); do
  [ -e /dev/fastrpc-adsp ] && [ "$(cat "$ADSP/state" 2>/dev/null)" = running ] && break
  sleep 1
done

log "sensor resume: restarting hexagonrpcd (adsp=$(cat "$ADSP/state" 2>/dev/null))"
systemctl restart hexagonrpcd-adsp-rootpd.service 2>/dev/null
sleep 2
systemctl restart hexagonrpcd-adsp-sensorspd.service 2>/dev/null
sleep 2
# The bridge's ssccli children die with the ADSP; restart it so the DBus
# properties keep updating.
systemctl try-restart rain-sensor-bridge.service 2>/dev/null
log "sensor resume: rootpd=$(systemctl is-active hexagonrpcd-adsp-rootpd) sensorspd=$(systemctl is-active hexagonrpcd-adsp-sensorspd)"
