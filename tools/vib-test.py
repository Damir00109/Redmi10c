#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
import ctypes, fcntl, os, struct, time

class ff_replay(ctypes.Structure):
    _fields_ = [("length", ctypes.c_uint16), ("delay", ctypes.c_uint16)]

class ff_trigger(ctypes.Structure):
    _fields_ = [("button", ctypes.c_uint16), ("interval", ctypes.c_uint16)]

class ff_rumble(ctypes.Structure):
    _fields_ = [("strong_magnitude", ctypes.c_uint16),
                ("weak_magnitude", ctypes.c_uint16)]

class ff_effect(ctypes.Structure):
    class _u(ctypes.Union):
        _fields_ = [("rumble", ff_rumble), ("_pad", ctypes.c_uint64 * 4)]
    _fields_ = [
        ("type", ctypes.c_uint16),
        ("id", ctypes.c_int16),
        ("direction", ctypes.c_uint16),
        ("trigger", ff_trigger),
        ("replay", ff_replay),
        ("u", _u),
    ]

path = None
for i in range(16):
    try:
        name = open(f"/sys/class/input/event{i}/device/name").read().strip()
    except OSError:
        continue
    if name == "gpio-vibrator":
        path = f"/dev/input/event{i}"
        break
if not path:
    raise SystemExit("gpio-vibrator not found")

fd = os.open(path, os.O_RDWR)
e = ff_effect()
e.type = 0x50  # FF_RUMBLE
e.id = -1
e.replay.length = 600
e.u.rumble.strong_magnitude = 0xFFFF
e.u.rumble.weak_magnitude = 0xFFFF
size = ctypes.sizeof(ff_effect)
ioc = (1 << 30) | (ord("E") << 8) | 0x80 | (size << 16)
fcntl.ioctl(fd, ioc, e)
print(f"play {path} id={e.id}")
os.write(fd, struct.pack("llHHi", 0, 0, 0x15, e.id, 1))
time.sleep(0.6)
os.write(fd, struct.pack("llHHi", 0, 0, 0x15, e.id, 0))
print("done")
os.close(fd)
