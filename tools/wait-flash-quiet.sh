#!/usr/bin/env bash
# Wait for fastboot, flash known-good USB boot once (no loops).
set -euo pipefail
ROOT=/home/damir00109/Desktop/Redmi10c
exec "$ROOT/tools/flash-once.sh" --wait --img "$ROOT/out/boot-usbtry3.img"
