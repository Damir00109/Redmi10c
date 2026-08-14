#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
set -e
cd "$(dirname "$0")"
aarch64-linux-gnu-gcc -O2 -Wall -Wextra -o touch_rings touch_rings.c
echo "built: $(pwd)/touch_rings"
ls -la touch_rings
