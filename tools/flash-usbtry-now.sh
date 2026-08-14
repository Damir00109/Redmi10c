#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Flash working USB try image when already in fastboot (or --wait).
set -euo pipefail
ROOT=/home/damir00109/Desktop/Redmi10c
IMG=$ROOT/out/boot-usbtry3.img
# prefer frozen known-good if present
[ -f "$ROOT/out/boot-usbtry3-thin-ok.img" ] && IMG=$ROOT/out/boot-usbtry3-thin-ok.img
WAIT=()
[ "${1:-}" = "--wait" ] && WAIT=(--wait)
exec "$ROOT/tools/flash-once.sh" "${WAIT[@]}" --img "$IMG"
