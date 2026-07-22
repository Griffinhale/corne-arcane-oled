import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Shell from 'gi://Shell';

const BUS = 'io.github.Griffinhale.CorneArcane';
const PATH = '/io/github/Griffinhale/CorneArcane';
const IFACE = 'io.github.Griffinhale.CorneArcane.Focus';

export default class CorneArcaneFocusBridge extends Extension {
    enable() {
        this._display = global.display;
        this._tracker = Shell.WindowTracker.get_default();
        this._signal = this._display.connect('notify::focus-window', () => this._report());
        this._report();
    }

    _report() {
        try {
            const window = this._display.focus_window;
            const app = window ? this._tracker.get_window_app(window) : null;
            const resourceClass = window?.get_wm_class() || '';
            const desktopId = app?.get_id() || '';
            Gio.DBus.session.call(
                BUS, PATH, IFACE, 'ReportActiveWindow',
                new GLib.Variant('(ss)', [resourceClass, desktopId]),
                null, Gio.DBusCallFlags.NONE, 1000, null, null,
            );
        } catch (_) {
            // A denied/absent session bus disables only this report.
        }
    }

    disable() {
        if (this._signal)
            this._display.disconnect(this._signal);
        this._signal = 0;
        this._display = null;
        this._tracker = null;
    }
}
