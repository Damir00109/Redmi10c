#!/bin/bash
# Regenerate kernel.patch from the applied source tree.
# ALWAYS intents-to-add the full list of new (untracked) files first.
set -e
cd "$(dirname "$0")/out/src/linux"
git add -N \
	arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog-sm6115.dts \
	arch/arm64/boot/dts/qcom/sm6225-xiaomi-fog.dts \
	arch/arm64/boot/dts/qcom/sm6225-xiaomi-rain.dts \
	arch/arm64/boot/dts/qcom/sm6225.dtsi \
	arch/arm64/configs/rain_defconfig \
	drivers/input/touchscreen/fts_spi \
	drivers/misc/aw87xxx-mini.c \
	drivers/pinctrl/qcom/pinctrl-khaje.c \
	drivers/power/supply/sh366101_fg_bringup.c \
	drivers/power/supply/smb1351_charger_bringup.c
git diff > ../../../kernel.patch
git reset --quiet
grep -c '^diff' ../../../kernel.patch
