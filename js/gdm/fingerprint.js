// -*- mode: js; js-indent-level: 4; indent-tabs-mode: nil -*-

const Gio = imports.gi.Gio;
const Lang = imports.lang;
const Shell = imports.gi.Shell;
const Signals = imports.signals;

const FprintManagerIface = <interface name='net.reactivated.Fprint.Manager'>
<method name='GetDefaultDevice'>
    <arg type='o' direction='out' />
</method>
</interface>;

const FprintManager = new Gio.DBusProxyClass({
    Name: 'FprintManager',
    Interface: FprintManagerIface,
    BusType: Gio.BusType.SYSTEM,
    BusName: 'net.reactivated.Fprint',
    ObjectPath: '/net/reactivated/Fprint/Manager'
});
