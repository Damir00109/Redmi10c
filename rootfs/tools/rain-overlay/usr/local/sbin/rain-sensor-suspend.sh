#!/bin/bash
# Release the fastrpc session before system sleep.
#
# Root cause of the ADSP crash on resume (found 2026-10-18): it is not the
# DSP losing power and not a missing sleep notification, but the fastrpc
# session that hexagonrpcd keeps open across the sleep - the DSP's frpc
# service faults right after resume ("EX:sensor_process:...frpc_dsp",
# "EX:kernel:...irq46") and remoteproc restarts the whole ADSP, which drags
# SoundWire/WCD937x through a noisy re-init.
#
# Stopping the sensor services before the sleep releases the session: with
# this in place the ADSP survives the resume untouched.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LOG=/var/log/rain-resume-probe.log
echo "$(date -Is) sensor suspend: stopping hexagonrpcd before sleep" >> "$LOG"
systemctl stop hexagonrpcd-adsp-sensorspd.service hexagonrpcd-adsp-rootpd.service 2>/dev/null
echo "$(date -Is) sensor suspend: rootpd=$(systemctl is-active hexagonrpcd-adsp-rootpd) sensorspd=$(systemctl is-active hexagonrpcd-adsp-sensorspd)" >> "$LOG"
