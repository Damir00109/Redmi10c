#!/bin/sh
FMT="SGRBG10_1X10/4080x3072"
for pad in \
  's5kjn1 0-0010:0' \
  'msm_csiphy0:0' 'msm_csiphy0:1' \
  'msm_csid0:0' 'msm_csid0:1' \
  'msm_vfe0_rdi0:0' 'msm_vfe0_rdi0:1'
do
  e="${pad%:*}"; p="${pad##*:}"
  media-ctl -d /dev/media0 --set-v4l2 "\"$e\":$p[fmt:$FMT]"
done
