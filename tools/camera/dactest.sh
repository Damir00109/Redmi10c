#!/bin/sh
# GT9764 blind DAC test: write DAC via CCI0 0x18, capture frames, compare focus.
# usage: dactest.sh <dac_lo> <dac_hi>
DEV=/dev/i2c-0
ACT=0x18
LO=${1:-0}
HI=${2:-1023}
CAP=/dev/video0

setup() {
  FMT="SGRBG10_1X10/4080x3072"
  media-ctl -d /dev/media0 -l '"msm_csiphy0":1 -> "msm_csid0":0 [1]' >/dev/null 2>&1
  media-ctl -d /dev/media0 -l '"msm_csid0":1 -> "msm_vfe0_rdi0":0 [1]' >/dev/null 2>&1
  for spec in '"s5kjn1 0-0010":0' '"msm_csiphy0":0' '"msm_csiphy0":1' \
              '"msm_csid0":0' '"msm_csid0":1' '"msm_vfe0_rdi0":0' '"msm_vfe0_rdi0":1'; do
    media-ctl -d /dev/media0 --set-v4l2 "$spec [fmt:$FMT]" >/dev/null 2>&1
  done
}

wr() { /tmp/eetool $DEV w8 $ACT "$1" "$2" >/dev/null 2>&1; }

capture() {
  v4l2-ctl -d $CAP --set-fmt-video=width=4080,height=3072,pixelformat=pgAA \
    --stream-mmap=4 --stream-count=1 --stream-to="$1" >/dev/null 2>&1
  ls -la "$1" 2>/dev/null
}

setup

# wake the VCM: PD mode off, ring mode off
wr 0x02 0x00

wr 0x03 $(( (LO >> 8) & 0x03 ))
wr 0x04 $(( LO & 0xff ))
sleep 0.2
echo "--- DAC=$LO ---"
capture /tmp/dac_lo.raw

wr 0x03 $(( (HI >> 8) & 0x03 ))
wr 0x04 $(( HI & 0xff ))
sleep 0.2
echo "--- DAC=$HI ---"
capture /tmp/dac_hi.raw

# back to middle
wr 0x03 0x01
wr 0x04 0x00
