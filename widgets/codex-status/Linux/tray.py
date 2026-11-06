"""Native StatusNotifierItem and DBusMenu, using the existing PyGObject dependency."""
from gi.repository import Gio, GLib

XML = """<node>
<interface name="org.kde.StatusNotifierItem">
 <method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
 <method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
 <method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
 <property name="Category" type="s" access="read"/><property name="Id" type="s" access="read"/>
 <property name="Title" type="s" access="read"/><property name="Status" type="s" access="read"/>
 <property name="IconName" type="s" access="read"/><property name="IconPixmap" type="a(iiay)" access="read"/>
 <property name="Menu" type="o" access="read"/><property name="ItemIsMenu" type="b" access="read"/>
 <property name="ToolTip" type="(sa(iiay)ss)" access="read"/>
 <signal name="NewIcon"/><signal name="NewStatus"><arg type="s"/></signal>
</interface>
<interface name="com.canonical.dbusmenu">
 <method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/><arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
 <method name="GetGroupProperties"><arg type="ai" direction="in"/><arg type="as" direction="in"/><arg type="a(ia{sv})" direction="out"/></method>
 <method name="GetProperty"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="out"/></method>
 <method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
 <method name="EventGroup"><arg type="a(isvu)" direction="in"/><arg type="ai" direction="out"/></method>
 <method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
 <method name="AboutToShowGroup"><arg type="ai" direction="in"/><arg type="ai" direction="out"/><arg type="ai" direction="out"/></method>
 <property name="Version" type="u" access="read"/><property name="TextDirection" type="s" access="read"/>
 <property name="Status" type="s" access="read"/><property name="IconThemePath" type="as" access="read"/>
 <signal name="LayoutUpdated"><arg type="u"/><arg type="i"/></signal>
</interface></node>"""


class StatusTray:
    def __init__(self, callbacks):
        self.callbacks = callbacks
        self.connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        self.ids = []
        self.watch_id = 0
        info = Gio.DBusNodeInfo.new_for_xml(XML)
        try:
            for interface in info.interfaces:
                path = "/StatusNotifierItem" if interface.name == "org.kde.StatusNotifierItem" else "/MenuBar"
                self.ids.append(self.connection.register_object(path, interface, self.call, self.get_property, None))
            self.connection.call_sync("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher",
                                      "RegisterStatusNotifierItem", GLib.Variant("(s)", (self.connection.get_unique_name(),)),
                                      None, Gio.DBusCallFlags.NONE, 2000, None)
            def watcher_changed(connection, sender, path, interface, signal, parameters):
                name, old_owner, new_owner = parameters.unpack()
                if old_owner and not new_owner:
                    self.callbacks["open"]()
                    self.close()
            self.watch_id=self.connection.signal_subscribe("org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                                                          "/org/freedesktop/DBus", "org.kde.StatusNotifierWatcher",
                                                          Gio.DBusSignalFlags.NONE, watcher_changed)
        except Exception:
            self.close()
            raise

    def get_property(self, connection, sender, path, interface, name):
        if interface == "org.kde.StatusNotifierItem":
            properties = {"Category": ("s", "ApplicationStatus"), "Id": ("s", "openai-usage"),
                          "Title": ("s", "OpenAI Usage"), "Status": ("s", "Active"),
                          "IconName": ("s", "biz.umbertogiacobbi.OpenAIUsage"), "IconPixmap": ("a(iiay)", []),
                          "Menu": ("o", "/MenuBar"), "ItemIsMenu": ("b", False),
                          "ToolTip": ("(sa(iiay)ss)", ("biz.umbertogiacobbi.OpenAIUsage", [], "OpenAI Usage", "Click to open"))}
        else:
            properties = {"Version": ("u", 3), "TextDirection": ("s", "ltr"), "Status": ("s", "normal"), "IconThemePath": ("as", [])}
        signature, value = properties[name]
        return GLib.Variant(signature, value)

    @staticmethod
    # No parameters: return the native menu's stable command identities.
    def items():
        return {1: "Open", 3: "About OpenAI Usage", 4: "Settings", 8: "Cody companion", 5: "Website", 6: "VibeWare manifesto", 7: "Exit"}

    @property
    def available(self):
        return bool(self.ids and self.watch_id)

    def props(self, item):
        if item == 0:
            return {"children-display": GLib.Variant("s", "submenu")}
        return {"label": GLib.Variant("s", self.items()[item]), "enabled": GLib.Variant("b", True), "visible": GLib.Variant("b", True)}

    # item: Stable native menu identity selected by the user.
    def activate(self, item):
        key = {1: "open", 3: "about", 4: "settings", 8: "cody", 5: "website", 6: "manifesto", 7: "exit"}.get(item)
        if key:
            GLib.idle_add(lambda: (self.callbacks[key](), False)[1])

    def call(self, connection, sender, path, interface, method, parameters, invocation):
        try:
            args = parameters.unpack()
            if interface == "org.kde.StatusNotifierItem":
                if method in ("Activate", "SecondaryActivate"):
                    self.activate(1)
                invocation.return_value(None)
            elif method == "GetLayout":
                parent = args[0]
                children = [GLib.Variant("(ia{sv}av)", (item, self.props(item), [])) for item in self.items()] if parent == 0 else []
                invocation.return_value(GLib.Variant("(u(ia{sv}av))", (1, (parent, self.props(parent), children))))
            elif method == "GetGroupProperties":
                invocation.return_value(GLib.Variant("(a(ia{sv}))", ([(item, self.props(item)) for item in args[0] if item in self.items() or item == 0],)))
            elif method == "GetProperty":
                invocation.return_value(GLib.Variant("(v)", (self.props(args[0])[args[1]],)))
            elif method == "Event":
                if args[1] == "clicked":
                    self.activate(args[0])
                invocation.return_value(None)
            elif method == "EventGroup":
                for item, event, data, timestamp in args[0]:
                    if event == "clicked":
                        self.activate(item)
                invocation.return_value(GLib.Variant("(ai)", ([],)))
            elif method == "AboutToShow":
                invocation.return_value(GLib.Variant("(b)", (False,)))
            elif method == "AboutToShowGroup":
                invocation.return_value(GLib.Variant("(aiai)", ([], [])))
            else:
                invocation.return_dbus_error("org.freedesktop.DBus.Error.UnknownMethod", method)
        except (KeyError, ValueError, TypeError):
            invocation.return_dbus_error("org.freedesktop.DBus.Error.InvalidArgs", "Unknown menu entry")

    def close(self):
        if self.watch_id:
            self.connection.signal_unsubscribe(self.watch_id)
            self.watch_id=0
        for registration in self.ids:
            self.connection.unregister_object(registration)
        self.ids.clear()
