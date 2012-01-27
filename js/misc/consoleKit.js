// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

const Gio = imports.gi.Gio;

const ConsoleKitManagerIface = <interface name='org.freedesktop.ConsoleKit.Manager'>
<method name='CanRestart'>
    <arg type='b' direction='out'/>
</method>
<method name='CanStop'>
    <arg type='b' direction='out'/>
</method>
<method name='Restart' />
<method name='Stop' />
<method name="GetCurrentSession">
    <arg type="o" direction="out" />
</method>
</interface>;

const ConsoleKitSessionIface = <interface name="org.freedesktop.ConsoleKit.Session">
<method name="IsActive">
    <arg type="b" direction="out" />
</method>
<signal name="ActiveChanged">
    <arg type="b" direction="out" />
</signal>
</interface>;

const ConsoleKitSessionProxy = new Gio.DBusProxyClass({
    Name: 'ConsoleKitSessionProxy',
    Interface: ConsoleKitSessionIface,
    BusType: Gio.BusType.SYSTEM,
    BusName: 'org.freedesktop.ConsoleKit'
});

const ConsoleKitManager = new Gio.DBusProxyClass({
    Name: 'ConsoleKitManager',
    Interface: ConsoleKitManagerIface,
    BusType: Gio.BusType.SYSTEM,
    BusName: 'org.freedesktop.ConsoleKit',
    ObjectPath: '/org/freedesktop/ConsoleKit/Manager',
    Flags: (Gio.DBusProxyFlags.DO_NOT_AUTO_START |
            Gio.DBusProxyFlags.DO_NOT_LOAD_PROPERTIES),

    _init: function() {
        this.connect('notify::g-name-owner', Lang.bind(this, this._onGNameOwnerChanged));
        this.parent.apply(this, arguments);
    },

    _onGNameOwnerChanged: function() {
        if (this.g_name_owner) {
            this.GetCurrentSessionRemote(function([session]) {
                this._ckSession = new ConsoleKitSessionProxy({ g_object_path: session });

                this._ckSession.connectSignal('ActiveChanged', function(object, senderName, [isActive]) {
                    this.sessionActive = isActive;
                });
                this._ckSession.IsActiveRemote(function([isActive]) {
                    this.sessionActive = isActive;
                });
            });
        } else {
            this.sessionActive = true;
        }
    }
});
