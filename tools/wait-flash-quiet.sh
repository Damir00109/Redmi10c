#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
# Wait for fastboot, flash known-good USB boot once (no loops).
set -euo pipefail
ROOT=/home/damir00109/Desktop/Redmi10c
exec "$ROOT/tools/flash-once.sh" --wait --img "$ROOT/out/boot-usbtry3.img"
