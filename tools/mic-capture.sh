#!/usr/bin/env bash
# Microphone capture for Redmi 10C (SM6225) — mainline audio bring-up.
#
# The WCD937x TX path needs several enum/bool kcontrols that DAPM does NOT
# set automatically (they default to ZERO/MSM_DMIC). Without them the PCM
# stream runs but delivers no data (arecord -> EIO / 44-byte WAV).
#
# Usage on device (adb shell):
#   CARD=XiaomiRedmi10C bash mic-capture.sh [rate] [seconds] [outfile]
# Defaults: 48000 Hz, 5 s, /tmp/mic.wav
#
# Verified: 48 kHz and 16 kHz mono S16_LE produce real PCM (RMS ~500+).

set -euo pipefail
CARD="${CARD:-XiaomiRedmi10C}"
RATE="${1:-48000}"
SECS="${2:-5}"
OUT="${3:-/tmp/mic.wav}"

amixer -c "$CARD" cset name='TX SMIC MUX0'      SWR_MIC0  >/dev/null   # ADC1 -> SWR input 0
amixer -c "$CARD" cset name='TX DEC0 MUX'        SWR_MIC   >/dev/null   # DEC0 <- SMIC MUX0
amixer -c "$CARD" cset name='TX_AIF1_CAP Mixer DEC0' 1      >/dev/null # DEC0 -> AIF1 ch0
amixer -c "$CARD" cset name='ADC1 Switch'        1          >/dev/null   # codec ADC1 on
amixer -c "$CARD" cset name='ADC1_MIXER Switch'  1          >/dev/null
amixer -c "$CARD" cset name='ADC1 Volume'        20         >/dev/null   # 0..20, 0.25 dB/step

exec arecord -D "hw:CARD=${CARD},DEV=0" -f S16_LE -r "$RATE" -c 1 -d "$SECS" "$OUT"
