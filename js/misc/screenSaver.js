// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

const Gio = imports.gi.Gio;
const Lang = imports.lang;

const ScreenSaverIface = <interface name="org.gnome.ScreenSaver">
<method name="GetActive">
    <arg type="b" direction="out" />
</method>
<method name="Lock" />
<method name="SetActive">
    <arg type="b" direction="in" />
</method>
<signal name="ActiveChanged">
    <arg type="b" direction="out" />
</signal>
</interface>;

const ScreenSaverProxy = new Gio.DBusProxyClass({
    Name: 'ScreenSaverProxy',
    Interface: ScreenSaverIface,
    BusType: Gio.BusType.SESSION,
    BusName: 'org.gnome.ScreenSaver',
    ObjectPath: '/org/gnome/ScreenSaver',
    Flags: (Gio.DBusProxyFlags.DO_NOT_AUTO_START |
            Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES),

    _init: function() {
        this.parent();
        this.screenSaverActive = false;
        this.connectSignal('ActiveChanged', Lang.bind(this, this._onActiveChanged));
        this.connect('notify::g-name-owner', Lang.bind(this, this._onGNameOwnerChanged));
    },

    _onActiveChanged: function(proxy, senderName, [isActive]) {
        this.screenSaverActive = isActive;
    },

    _onGNameOwnerChanged: function() {
        if (self.g_name_owner) {
            self.GetActiveRemote(function(result, excp) {
                if (result) {
                    let [isActive] = result;
                    self.screenSaverActive = isActive;
                }
            });
        } else {
            self.screenSaverActive = false;
        }
    }
});
