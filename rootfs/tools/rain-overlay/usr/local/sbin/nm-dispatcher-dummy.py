#!/usr/bin/python3
# No-op NetworkManager dispatcher.
# Owns org.freedesktop.nm_dispatcher so NetworkManager can call
# Action()/Action2() calls without failing, but runs no scripts.
import dbus
import dbus.service
import dbus.mainloop.glib
from gi.repository import GLib

IFACE = 'org.freedesktop.nm_dispatcher'
PATH = '/org/freedesktop/nm_dispatcher'


class NMDispatcher(dbus.service.Object):
    def __init__(self, bus, path):
        super().__init__(bus, path)

    @dbus.service.method(IFACE,
                         in_signature='sa{sa{sv}}a{sv}a{sv}a{sv}a{sv}a{sv}a{sv}sa{sv}a{sv}b',
                         out_signature='a(sus)')
    def Action(self, _a, _b, _c, _d, _e, _f, _g, _h, _i, _j, _k, _l):
        return dbus.Array([], signature='(sus)')

    @dbus.service.method(IFACE,
                         in_signature='sa{sa{sv}}a{sv}a{sv}a{sv}a{sv}a{sv}a{sv}sa{sv}a{sv}b',
                         out_signature='a(susa{sv})')
    def Action2(self, _a, _b, _c, _d, _e, _f, _g, _h, _i, _j, _k, _l):
        return dbus.Array([], signature='(susa{sv})')


if __name__ == '__main__':
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    name = dbus.service.BusName(IFACE, bus)
    dispatcher = NMDispatcher(bus, PATH)
    GLib.MainLoop().run()
