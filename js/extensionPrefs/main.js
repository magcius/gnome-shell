
const Lang = imports.lang;
const Gettext = imports.gettext;
const GLib = imports.gi.GLib;
const GObject = imports.gi.GObject;
const Gio = imports.gi.Gio;
const Gtk = imports.gi.Gtk;
const Pango = imports.gi.Pango;

const _ = Gettext.gettext;

const Config = imports.misc.config;
const Format = imports.misc.format;
const ExtensionUtils = imports.misc.extensionUtils;


const GnomeShellIface = <interface name="org.gnome.Shell">
<signal name="ExtensionStatusChanged">
    <arg type="s" name="uuid"/>
    <arg type="i" name="state"/>
    <arg type="s" name="error"/>
</signal>
</interface>;

const GnomeShellProxy = new Gio.DBusProxyClass({
    Name: 'GnomeShellProxy',
    Interface: GnomeShellIface,
    BusType: Gio.BusType.SESSION,
    BusName: 'org.gnome.Shell',
    ObjectPath: '/org/gnome/Shell'
});

// Ugly hack until GTK+ is fixed to support arbitrary
// CSS on any widget.
function styleMe(widget, css) {
    // frame understands border, but not background
    let frame = new Gtk.Frame();
    frame.add(widget);

    // eventbox understands background, but not border
    let ev = new Gtk.EventBox();
    ev.add(frame);

    let prov = new Gtk.CssProvider();
    prov.load_from_data('GtkFrame, GtkEventBox { ' + css + ' }');

    let sc1 = ev.get_style_context();
    let sc2 = frame.get_style_context();
    sc1.add_provider(prov, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);
    sc2.add_provider(prov, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION);

    return ev;
}

const Application = new Lang.Class({
    Name: 'Application',
    _init: function() {
        GLib.set_prgname('gnome-shell-extension-prefs');
        this.application = new Gtk.Application({
            application_id: 'org.gnome.shell.ExtensionPrefs',
            flags: Gio.ApplicationFlags.HANDLES_COMMAND_LINE
        });

        this.application.connect('activate', Lang.bind(this, this._onActivate));
        this.application.connect('command-line', Lang.bind(this, this._onCommandLine));
        this.application.connect('startup', Lang.bind(this, this._onStartup));

        this._extensionMetas = {};
        this._extensionPrefsModules = {};

        this._extensionIters = {};
    },

    _buildModel: function() {
        this._model = new Gtk.ListStore();
        this._model.set_column_types([GObject.TYPE_STRING, GObject.TYPE_STRING]);
    },

    _extensionAvailable: function(uuid) {
        let meta = this._extensionMetas[uuid];

        if (!meta)
            return false;

        if (ExtensionUtils.isOutOfDate(meta))
            return false;

        if (!meta.dir.get_child('prefs.js').query_exists(null))
            return false;

        return true;
    },

    _setExtensionInsensitive: function(layout, cell, model, iter, data) {
        let uuid = model.get_value(iter, 0);
        if (!this._extensionAvailable(uuid))
            cell.set_sensitive(false);
    },

    _getExtensionPrefsModule: function(meta) {
        if (this._extensionPrefsModules.hasOwnProperty(meta.uuid))
            return this._extensionPrefsModules[meta.uuid];

        ExtensionUtils.installImporter(meta);

        let prefsModule = meta.importer.prefs;
        prefsModule.init(meta);

        this._extensionPrefsModules[meta.uuid] = prefsModule;
        return prefsModule;
    },

    _selectExtension: function(uuid) {
        if (!this._extensionAvailable(uuid))
            return;

        let meta = this._extensionMetas[uuid];
        let prefsModule = this._getExtensionPrefsModule(meta);
        let widget;

        try {
            widget = prefsModule.buildPrefsWidget(meta);
        } catch (e) {
            widget = this._buildErrorUI(meta, e);
        }

        // Destroy the current prefs widget, if it exists
        if (this._extensionPrefsBin.get_child())
            this._extensionPrefsBin.get_child().destroy();

        this._extensionPrefsBin.add(widget);
        this._extensionSelector.set_active_iter(this._extensionIters[uuid]);
    },

    _extensionSelected: function() {
        let [success, iter] = this._extensionSelector.get_active_iter();
        if (!success)
            return;

        let uuid = this._model.get_value(iter, 0);
        this._selectExtension(uuid);
    },

    _buildErrorUI: function(meta, exc) {
        let box = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL });
        let label = new Gtk.Label({
            label: _("There was an error loading the preferences dialog for %s:").format(meta.name)
        });
        box.add(label);

        let errortext = '';
        errortext += exc;
        errortext += '\n\n';
        errortext += 'Stack trace:\n';

        // Indent stack trace.
        errortext += exc.stack.split('\n').map(function(line) {
            return '  ' + line;
        }).join('\n');

        let scroll = new Gtk.ScrolledWindow({ vexpand: true });
        let buffer = new Gtk.TextBuffer({ text: errortext });
        let textview = new Gtk.TextView({ buffer: buffer });
        textview.override_font(Pango.font_description_from_string('monospace'));
        scroll.add(textview);
        box.add(scroll);

        box.show_all();
        return box;
    },

    _buildUI: function(app) {
        this._window = new Gtk.ApplicationWindow({ application: app,
                                                   window_position: Gtk.WindowPosition.CENTER,
                                                   title: _("GNOME Shell Extension Preferences") });

        this._window.set_size_request(600, 400);

        let vbox = new Gtk.Box({ orientation: Gtk.Orientation.VERTICAL });
        this._window.add(vbox);

        let topBar = new Gtk.Box({ orientation: Gtk.Orientation.HORIZONTAL,
                                   hexpand: true,
                                   border_width: 8,
                                   spacing: 8 });

        let topBarStyle = '';
        topBarStyle += 'background-color: shade (@theme_bg_color, 0.95);\n';
        topBarStyle += 'border-bottom: 1px solid shade (@theme_bg_color, 0.8);\n';

        vbox.add(styleMe(topBar, topBarStyle));

        let label = new Gtk.Label({ label: _("<b>Extension</b>"),
                                    use_markup: true });
        topBar.add(label);

        this._extensionSelector = new Gtk.ComboBox({ model: this._model,
                                                     hexpand: true });

        let renderer = new Gtk.CellRendererText();
        this._extensionSelector.pack_start(renderer, true);
        this._extensionSelector.add_attribute(renderer, 'text', 1);
        this._extensionSelector.set_cell_data_func(renderer, Lang.bind(this, this._setExtensionInsensitive), null);
        this._extensionSelector.connect('changed', Lang.bind(this, this._extensionSelected));
        topBar.add(this._extensionSelector);

        this._extensionPrefsBin = new Gtk.Frame();
        vbox.add(this._extensionPrefsBin);

        let label = new Gtk.Label({
            label: _("Select an extension to configure using the combobox above."),
            vexpand: true
        });

        this._extensionPrefsBin.add(label);

        this._shellProxy = new GnomeShellProxy();
        this._shellProxy.connectSignal('ExtensionStatusChanged', Lang.bind(this, function(proxy, senderName, [uuid, state, error]) {
            if (!this._extensionMetas.hasOwnProperty(uuid))
                this._scanExtensions();
        }));

        this._window.show_all();
    },

    _scanExtensions: function() {
        ExtensionUtils.scanExtensions(Lang.bind(this, function(uuid, dir, type) {
            if (this._extensionMetas.hasOwnProperty(uuid))
                return;

            let meta;
            try {
                meta = ExtensionUtils.loadMetadata(uuid, dir, type);
            } catch(e) {
                global.logError('' + e);
                return;
            }

            this._extensionMetas[uuid] = meta;

            let iter = this._model.append();
            this._model.set(iter, [0, 1], [uuid, meta.name]);
            this._extensionIters[uuid] = iter;
        }));
    },

    _onActivate: function() {
        this._window.present();
    },

    _onStartup: function(app) {
        this._buildModel();
        this._buildUI(app);
        this._scanExtensions();
    },

    _onCommandLine: function(app, commandLine) {
        app.activate();
        let args = commandLine.get_arguments();
        if (args.length) {
            let uuid = args[0];
            if (!this._extensionAvailable(uuid))
                return 1;

            this._selectExtension(uuid);
        }
        return 0;
    }
});

function initEnvironment() {
    // Monkey-patch in a "global" object that fakes some Shell utilities
    // that ExtensionUtils depends on.
    window.global = {
        log: function() {
            print([].join.call(arguments, ', '));
        },

        logError: function(s) {
            global.log('ERROR: ' + s);
        },

        userdatadir: GLib.build_filenamev([GLib.get_user_data_dir(), 'gnome-shell'])
    };

    String.prototype.format = Format.format;
}

function main(argv) {
    initEnvironment();
    ExtensionUtils.init();

    Gettext.bindtextdomain(Config.GETTEXT_PACKAGE, Config.LOCALEDIR);
    Gettext.textdomain(Config.GETTEXT_PACKAGE);

    let app = new Application();
    app.application.run(argv);
}
