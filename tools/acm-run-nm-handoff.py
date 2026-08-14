#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
import serial, time, sys

ser = serial.Serial()
ser.port = "/dev/ttyACM0"
ser.baudrate = 115200
ser.timeout = 0.4
ser.write_timeout = 5
ser.dsrdtr = True
ser.open()


def drain(t=1.0):
    end = time.time() + t
    o = b""
    while time.time() < end:
        d = ser.read(8192)
        if d:
            o += d
        else:
            time.sleep(0.03)
    while True:
        d = ser.read(8192)
        if not d:
            break
        o += d
    return o.decode(errors="replace")


def cmd(c, wait=2.0):
    ser.write((c + "\r\n").encode())
    o = drain(wait)
    sys.stdout.write(o[-4000:] if len(o) > 4000 else o)
    sys.stdout.flush()
    return o


def login():
    ser.write(b"\x03\r\n")
    drain(0.4)
    for _ in range(20):
        ser.write(b"\r\n")
        o = drain(0.8)
        if "rain@" in o:
            return True
        if "Password:" in o and "login:" not in o.lower():
            time.sleep(0.3)
            ser.write(b"rain\r\n")
            if "rain@" in drain(2.5):
                return True
            continue
        if "login:" in o.lower():
            ser.write(b"rain\r\n")
            o = drain(1.2)
            if "Password:" in o:
                time.sleep(0.4)
                ser.write(b"rain\r\n")
                o = drain(3)
            if "rain@" in o or "Welcome" in o:
                return True
    return False


assert login()
print("SHELL_OK")
cmd("stty -echo; export TERM=linux", 1)
cmd("printf 'rain\\n' | sudo -S -v; echo SUDO_OK", 3)

# Phase 1: start stack (may take ~90s)
print("\n===== WIFI START =====")
o = cmd("printf 'rain\\n' | sudo -S /usr/local/sbin/qcom-wifi-start.sh; echo START_RC:$?", 120)
if "OK_wlan0" not in o and "START_RC:0" not in o:
    print("START_MAYBE_FAIL — continue if wlan0 exists")

print("\n===== WIFI CONNECT =====")
o = cmd("printf 'rain\\n' | sudo -S /usr/local/sbin/qcom-wifi-connect.sh; echo CONN_RC:$?", 90)

print("\n===== PING CHECK =====")
cmd("ping -c2 -W3 1.1.1.1; ip -br addr show wlan0; echo PING_DONE", 8)

print("\n===== NM HANDOFF =====")
o = cmd("printf 'rain\\n' | sudo -S /usr/local/sbin/qcom-wifi-nm-handoff.sh; echo NM_RC:$?", 90)

print("\n===== FINAL =====")
cmd(
    "ip -br addr; nmcli -t -f DEVICE,STATE,CONNECTION device 2>/dev/null; "
    "ping -c2 -W3 1.1.1.1; ping -c1 -W3 ya.ru; echo ALL_DONE",
    12,
)
ser.close()
print("RUNNER_DONE")
