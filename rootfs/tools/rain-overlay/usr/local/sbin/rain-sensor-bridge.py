#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 Damir00109 <internet00109@gmail.com>
# https://github.com/Damir00109/Redmi10c
#
# Bridge the Snapdragon Sensor Core (SSC) into the interface desktops expect.
#
# The sensors on this board hang off the ADSP, not the AP i2c bus, so there are
# no kernel IIO devices and iio-sensor-proxy cannot see them.  hexagonrpcd +
# libssc do provide the data (ssccli streams it), so this daemon re-publishes
# the accelerometer and the ambient light sensor on the system bus with the
# net.hadess.SensorProxy interface - the same one iio-sensor-proxy implements
# and GNOME/Plasma/phosh consume.
import os
import re
import subprocess
import threading
import time

import dbus
import dbus.mainloop.glib
import dbus.service
from gi.repository import GLib

SSC_DIR = "/usr/bin"
SSC_ENV = dict(os.environ, LD_LIBRARY_PATH="/usr/lib/musl")
ACCEL_RE = re.compile(r"X=(-?[\d.]+) Y=(-?[\d.]+) Z=(-?[\d.]+)")
LIGHT_RE = re.compile(r"[Ll]ight.*?(-?[\d.]+)\s*(?:[Ll]ux|lux)")

# The SSC firmware ships an all-zero mount matrix; the axes come out rotated
# 180 degrees around Z relative to the chassis (same correction as
# /etc/udev/rules.d/90-accel-mount.rules for the iio path).
MOUNT = (-1.0, -1.0, 1.0)


class SensorProxy(dbus.service.Object):
    IFACE = "net.hadess.SensorProxy"

    def __init__(self, bus):
        super().__init__(bus, "/net/hadess/SensorProxy")
        self.lock = threading.Lock()
        self.accel = None
        self.light = None
        self.accel_claimed = False
        self.light_claimed = False

    # ---- data from the ssccli subprocesses ----
    def set_accel(self, x, y, z):
        x, y, z = (x * MOUNT[0], y * MOUNT[1], z * MOUNT[2])
        with self.lock:
            changed = self._orientation(x, y, z) != self._orientation(*self.accel) if self.accel else True
            self.accel = (x, y, z)
        if changed and self.accel_claimed:
            self.AccelerometerOrientation = self._orientation(x, y, z)
            self.PropertiesChanged(self.IFACE,
                                   {"AccelerometerOrientation": self.AccelerometerOrientation},
                                   [])

    def set_light(self, lux):
        with self.lock:
            self.light = lux
        if self.light_claimed:
            self.PropertiesChanged(self.IFACE, {"LightLevel": dbus.Double(lux)}, [])

    @staticmethod
    def _orientation(x, y, z):
        # orientation of the device's "up" edge, as iio-sensor-proxy reports it
        if x > 6.0:
            return "left-up"
        if x < -6.0:
            return "right-up"
        if y > 6.0:
            return "bottom-up"
        if y < -6.0:
            return "normal"
        if z < -6.0:
            return "bottom-up"
        return "normal"

    # ---- net.hadess.SensorProxy ----
    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ss", out_signature="v")
    def Get(self, iface, prop):
        return self.GetAll(iface)[prop]

    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, iface):
        with self.lock:
            accel = self.accel
            light = self.light
        return {
            "HasAccelerometer": dbus.Boolean(True),
            "HasAmbientLight": dbus.Boolean(True),
            "HasProximity": dbus.Boolean(False),
            "AccelerometerOrientation": dbus.String(
                self._orientation(*accel) if accel else "normal"),
            "LightLevel": dbus.Double(light if light is not None else -1.0),
            "LightLevelUnit": dbus.String("lux"),
        }

    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ssv")
    def Set(self, iface, prop, value):
        raise dbus.exceptions.DBusException("read-only")

    @dbus.service.method("net.hadess.SensorProxy", in_signature="", out_signature="")
    def ClaimAccelerometer(self):
        self.accel_claimed = True

    @dbus.service.method("net.hadess.SensorProxy", in_signature="", out_signature="")
    def ReleaseAccelerometer(self):
        self.accel_claimed = False

    @dbus.service.method("net.hadess.SensorProxy", in_signature="", out_signature="")
    def ClaimLight(self):
        self.light_claimed = True

    @dbus.service.method("net.hadess.SensorProxy", in_signature="", out_signature="")
    def ReleaseLight(self):
        self.light_claimed = False

    @dbus.service.method("net.hadess.SensorProxy", in_signature="", out_signature="")
    def ClaimProximity(self):
        pass

    @dbus.service.method("net.hadess.SensorProxy", in_signature="", out_signature="")
    def ReleaseProximity(self):
        pass

    @dbus.service.signal(dbus.PROPERTIES_IFACE, signature="sa{sv}as")
    def PropertiesChanged(self, iface, changed, invalidated):
        pass


def pump(sensor, handler):
    """Run ssccli for one sensor and feed every measurement to handler()."""
    print("rain-sensor-bridge: streaming %s" % sensor, flush=True)
    while True:
        try:
            proc = subprocess.Popen(
                [os.path.join(SSC_DIR, "ssccli"), "--sensor", sensor],
                env=SSC_ENV, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                text=True, bufsize=1)
            for line in proc.stdout:
                m = ACCEL_RE.search(line) if sensor == "accelerometer" else LIGHT_RE.search(line)
                if m:
                    handler(*[float(g) for g in m.groups()])
            proc.wait()
        except Exception:
            pass
        time.sleep(5)


def main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    proxy = SensorProxy(bus)
    # Keep a reference: a garbage-collected BusName releases the name again.
    proxy.busname = dbus.service.BusName("net.hadess.SensorProxy", bus)
    print("rain-sensor-bridge: owning net.hadess.SensorProxy", flush=True)

    threading.Thread(target=pump, args=("accelerometer", proxy.set_accel), daemon=True).start()
    threading.Thread(target=pump, args=("light", proxy.set_light), daemon=True).start()
    GLib.MainLoop().run()


if __name__ == "__main__":
    main()
