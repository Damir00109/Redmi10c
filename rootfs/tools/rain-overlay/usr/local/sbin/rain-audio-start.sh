#!/bin/bash
# Load ASoC audio modules for SM6225/WCD9375.
# Optional: if ADSP doesn't boot, Bluetooth audio still works via PulseAudio.
set +e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

echo "rain-audio: loading ASoC modules..."

# Ensure firmware symlinks are in place (rain-wifi-nm should have done this)
if [ ! -e /lib/firmware/qcom/sm6225/adsp.mdt ]; then
  for i in 1 2 3 4 5; do
    sleep 2
    [ -e /lib/firmware/qcom/sm6225/adsp.mdt ] && break
  done
fi

if [ ! -e /lib/firmware/qcom/sm6225/adsp.mdt ]; then
  echo "rain-audio: ADSP firmware not found — skipping (BT audio still works)"
  exit 0
fi

# Q6DSP core (APR → q6core/afe/asm/adm)
for m in qcom_apr q6core q6afe q6afe-dai q6afe-clocks \
         q6asm q6asm-dai q6adm q6routing; do
  modprobe "$m" 2>/dev/null || true
done

# LPASS macros
for m in snd-soc-lpass-macro-common snd-soc-lpass-va-macro \
         snd-soc-lpass-rx-macro snd-soc-lpass-tx-macro; do
  modprobe "$m" 2>/dev/null || true
done

# SoundWire + WCD9375 codec
for m in soundwire-qcom snd-soc-wcd937x snd-soc-wcd937x-sdw \
         snd-soc-qcom-common snd-soc-qcom-sdw snd-soc-sm8250; do
  modprobe "$m" 2>/dev/null || true
done

# ADSP remoteproc — load last so all consumers are ready
modprobe qcom_q6v5_adsp 2>/dev/null || true

# Wait for sound card
for i in 1 2 3 4 5 6 7 8 9 10; do
  sleep 1
  [ -d /sys/class/sound ] && break
done

cat /proc/asound/cards 2>/dev/null || echo "rain-audio: no ALSA sound cards (BT audio still works)"
echo "rain-audio: done"
