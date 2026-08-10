#!/usr/bin/env python3
"""Deploy ADB gadget bits to rain phone over ACM in small ACKed chunks (no Wi-Fi)."""
from __future__ import annotations

import argparse
import base64
import hashlib
import os
import sys
import time
from pathlib import Path

import serial

SERVE = Path(__file__).resolve().parent
CHUNK = 2048  # bytes of raw file per chunk (base64 ~2.7k) — ACM dies on megabyte writes
PASS = "rain"


def open_ser(port: str) -> serial.Serial:
    ser = serial.Serial(port, 115200, timeout=2, write_timeout=10)
    ser.reset_input_buffer()
    return ser


def drain(ser: serial.Serial, seconds: float = 0.3) -> str:
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
        else:
            time.sleep(0.05)
    return buf.decode(errors="replace")


def run(ser: serial.Serial, cmd: str, wait: float = 2.0, marker: str | None = None) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    buf = b""
    deadline = time.time() + wait
    while time.time() < deadline:
        chunk = ser.read(8192)
        if chunk:
            buf += chunk
            if marker and marker.encode() in buf:
                # allow prompt after marker
                time.sleep(0.2)
                buf += ser.read(8192)
                break
        else:
            time.sleep(0.05)
    text = buf.decode(errors="replace")
    print(f">>> {cmd[:120]}\n{text[-800:]}\n", flush=True)
    return text


def sudo(ser: serial.Serial, cmd: str, wait: float = 5.0, marker: str | None = None) -> str:
    return run(ser, f"echo {PASS} | sudo -S {cmd}", wait=wait, marker=marker)


def push_file(ser: serial.Serial, local: Path, remote: str) -> None:
    data = local.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    n = (len(data) + CHUNK - 1) // CHUNK
    print(f"push {local.name} -> {remote} ({len(data)} bytes, {n} chunks)", flush=True)
    run(ser, f"rm -f {remote}.b64 {remote}; touch {remote}.b64", wait=1.5)
    for i in range(n):
        part = data[i * CHUNK : (i + 1) * CHUNK]
        b64 = base64.b64encode(part).decode("ascii")
        # Append one line; keep command short
        cmd = f"printf '%s\\n' '{b64}' >> {remote}.b64"
        ser.reset_input_buffer()
        ser.write((cmd + "\n").encode())
        # Wait for shell prompt-ish: short read
        time.sleep(0.15)
        _ = ser.read(4096)
        if (i + 1) % 50 == 0 or i + 1 == n:
            print(f"  chunk {i+1}/{n}", flush=True)
    run(
        ser,
        f"base64 -d {remote}.b64 > {remote} && rm -f {remote}.b64 && "
        f"sha256sum {remote} | awk '{{print $1}}'",
        wait=30,
        marker=digest[:16],
    )
    out = run(ser, f"sha256sum {remote}", wait=3)
    if digest not in out:
        raise SystemExit(f"checksum mismatch for {remote}: expected {digest}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--serve", type=Path, default=SERVE)
    args = ap.parse_args()
    serve: Path = args.serve

    files = [
        ("sbin/usb-adb-gadget.sh", "/tmp/rain-adb-inst/sbin/usb-adb-gadget.sh"),
        ("sbin/rain-enable-adb-gadget.sh", "/tmp/rain-adb-inst/sbin/rain-enable-adb-gadget.sh"),
        ("sbin/install-adbd-debs.sh", "/tmp/rain-adb-inst/sbin/install-adbd-debs.sh"),
        ("systemd/usb-adb-gadget.service", "/tmp/rain-adb-inst/systemd/usb-adb-gadget.service"),
        ("systemd/adbd.service", "/tmp/rain-adb-inst/systemd/adbd.service"),
    ]
    debs = sorted((serve / "debs").glob("*.deb"))
    if len(debs) < 5:
        raise SystemExit(f"expected debs in {serve}/debs, found {debs}")

    ser = open_ser(args.port)
    drain(ser, 0.5)
    run(ser, "", wait=0.5)
    alive = run(ser, "echo ACM_ALIVE; uname -r", wait=3, marker="ACM_ALIVE")
    if "ACM_ALIVE" not in alive:
        raise SystemExit("phone not responding on ACM — reboot from display (getty@tty1) first")

    run(ser, "rm -rf /tmp/rain-adb-inst && mkdir -p /tmp/rain-adb-inst/sbin /tmp/rain-adb-inst/systemd /tmp/rain-adb-inst/debs", wait=2)

    for rel, rem in files:
        push_file(ser, serve / rel, rem)
    for deb in debs:
        push_file(ser, deb, f"/tmp/rain-adb-inst/debs/{deb.name}")

    sudo(
        ser,
        "bash -c '"
        "install -m 0755 /tmp/rain-adb-inst/sbin/* /usr/local/sbin/ && "
        "install -m 0644 /tmp/rain-adb-inst/systemd/*.service /etc/systemd/system/ && "
        "export DEBIAN_FRONTEND=noninteractive; "
        "dpkg -i /tmp/rain-adb-inst/debs/*.deb || apt-get -y -f install; "
        "/usr/local/sbin/rain-enable-adb-gadget.sh; "
        "sync; echo INSTALL_OK'",
        wait=120,
        marker="INSTALL_OK",
    )
    print("Rebooting phone...", flush=True)
    sudo(ser, "reboot", wait=2)
    ser.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
