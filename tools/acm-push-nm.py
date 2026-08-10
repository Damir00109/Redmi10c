#!/usr/bin/env python3
import serial, time, base64, pathlib, sys

SER = "/dev/ttyACM0"
ROOT = pathlib.Path("/home/damir00109/Desktop/Redmi10c/tools/rain-overlay")
FILES = [
    (ROOT / "usr/local/sbin/qcom-wifi-connect.sh", "/usr/local/sbin/qcom-wifi-connect.sh", "755"),
    (ROOT / "usr/local/sbin/qcom-wifi-nm-handoff.sh", "/usr/local/sbin/qcom-wifi-nm-handoff.sh", "755"),
    (ROOT / "etc/systemd/system/rain-wifi.service", "/etc/systemd/system/rain-wifi.service", "644"),
]

ser = serial.Serial(SER, 115200, timeout=0.35)


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
    sys.stdout.write(o[-3000:] if len(o) > 3000 else o)
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
            o = drain(2.5)
            if "rain@" in o:
                return True
            continue
        if "login:" in o.lower():
            time.sleep(0.2)
            ser.write(b"rain\r\n")
            o = drain(1.2)
            if "Password:" in o:
                time.sleep(0.4)
                ser.write(b"rain\r\n")
                o = drain(3)
            if "rain@" in o or "Welcome" in o:
                return True
    return False


assert login(), "login failed"
print("SHELL_OK")
cmd("stty -echo; export TERM=linux", 1)
cmd("printf 'rain\\n' | sudo -S -v; echo SUDO_RC:$?", 3)

for src, dest, mode in FILES:
    b64 = base64.b64encode(src.read_bytes()).decode()
    tmp = f"/tmp/{pathlib.Path(dest).name}.b64"
    print(f"\n### {dest} len={len(b64)} ###")
    cmd(f"rm -f {tmp}", 0.8)
    # one python write on device — faster than many printf
    # send via here-doc without sudo
    ser.write(f"cat >{tmp} <<'X'\r\n".encode())
    drain(0.3)
    for i in range(0, len(b64), 240):
        ser.write((b64[i : i + 240] + "\n").encode())
        time.sleep(0.01)
    ser.write(b"X\r\n")
    drain(1.0)
    cmd(f"wc -c {tmp}; base64 -d {tmp} | wc -c", 2)
    cmd(
        f"printf 'rain\\n' | sudo -S bash -c 'base64 -d {tmp} > {dest} && chmod {mode} {dest} && ls -l {dest}'",
        3,
    )

cmd("printf 'rain\\n' | sudo -S systemctl daemon-reload; echo RELOADED", 3)
cmd(
    "ip -br link; cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null; "
    "pgrep -a tqftpserv; pgrep -a wpa; ping -c1 -W2 1.1.1.1; echo PRE_STAT",
    6,
)
ser.close()
print("PUSH_DONE")
