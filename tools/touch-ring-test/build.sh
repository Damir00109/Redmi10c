#!/bin/sh
set -e
cd "$(dirname "$0")"
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -o touch_rings touch_rings.c
echo "built: $(pwd)/touch_rings"
ls -la touch_rings
