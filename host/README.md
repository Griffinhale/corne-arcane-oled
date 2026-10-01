# Corne Arcane host package

The optional host package sends bounded, privacy-redacted desktop and browser
activity semantics to `griffin_arcane`. Firmware remains fully functional when
it is absent.

The package provides the `corne-arcane` desktop app and the
`corne-arcane-host`, `corne-arcane-event`, `corne-arcane-diagnostics`,
`corne-arcane-vial`, `corne-arcane-keymap` and `corne-arcane-tray` commands, the
`io.github.Griffinhale.CorneArcane` D-Bus name, and the
`corne-arcane-host.service` user unit. `corne-arcane-focus-x11` is an opt-in
focus producer for X11 sessions, and `corne-arcane-tray` an opt-in tray icon.

Raw HID v3 is a 32-byte report with an eight-byte payload, including secondary
activity values for scroll, tab selection, and page events. The generic method
is usable without Firefox:

```bash
corne-arcane-event browser scroll 1
```

The daemon and the firmware must both speak Raw HID v3 (`VERSION` in
`arcane_host/protocol.py`, `DUEL_HOST_VERSION` in `firmware/sim/duel_host.h`).
Within v3 a newer daemon can still send a value that older firmware rejects,
so update the firmware first: flash both halves before upgrading the daemon.
[`../docs/flashing.md`](../docs/flashing.md#updating) has the full sequence.

The Observatory ritual uses a 1,500-second Pomodoro by default. Configure it
with `--pomodoro-duration SECONDS`, or `services.corne-arcane-host.pomodoroDuration`
when importing `corne.nix`.

## Installing

The install layout is defined once, in `Makefile`, and driven by both
`package.nix` and `../debian/rules`, so the two platforms cannot drift apart.

On NixOS, import [`../corne.nix`](../corne.nix). It supplies the package, the
udev rule, and the user service. On a session with no compositor focus bridge
also set `services.corne-arcane-host.x11FocusProducer = true;` -- NixOS builds
user units from module definitions instead of from the package, so the
producer's unit ships but is not declared without it. For the same reason the
tray icon needs `services.corne-arcane-host.trayIcon = true;`.

On Debian or Ubuntu, build the package from the repository root of a checkout:

```bash
sudo apt install build-essential debhelper python3-gi dbus-user-session
dpkg-buildpackage -b -uc -us
sudo apt install ../corne-arcane-host_*.deb
systemctl --user enable --now corne-arcane-host.service
```

Unplug and replug the keyboard after the package installs, so its udev rule
applies to the device.

`dbus-user-session` is required, not optional: the daemon is a systemd user
service (Type=dbus) and the diagnostics and Vial handoff both use `systemctl --user`.
It is present by default on desktop installs and absent on minimal ones.

To install without building a package, run this from the repository root:

```bash
make city-lib
sudo make -C host install PREFIX=/usr
```

`make city-lib` builds the city app's native library, which the install puts
beside the Python code. Skip it and every command but `corne-arcane` still
works; the install warns that the app will not start.

It places the same layout directly. Keep `PREFIX=/usr`: the default,
`/usr/local`, puts the Firefox native-messaging manifest and the udev rule
where Firefox and udev do not look. Unplug and replug the keyboard afterwards
so the udev rule applies.

On Arch or Fedora, install these first. The commands need the first three
packages; `xorg-xprop` or `xprop` is only for the X11 focus producer, and
`make` and `gcc` only build the city library.

```bash
sudo pacman -S --needed python python-gobject tk xorg-xprop make gcc          # Arch
sudo dnf install python3 python3-gobject-base python3-tkinter xprop make gcc  # Fedora
```

A desktop install of either already has the systemd user session and its
D-Bus. Neither distro has a `plugdev` group, so udev ignores the rule's
`GROUP="plugdev"` and logs that it did. The `uaccess` tag still gives the
logged-in desktop user the keyboard, which is the normal case. To also reach it
over ssh or `su`, create the group and join it, then log in again and replug
the keyboard:

```bash
sudo groupadd --system plugdev
sudo usermod -aG plugdev "$USER"
sudo udevadm control --reload
```

Then enable the service with `systemctl --user enable --now corne-arcane-host.service`.

Debian has no `vial` package -- Vial ships as an AppImage -- so tell
`corne-arcane-vial` where it is. Put the path, or a whole command such as
`flatpak run <app id>`, on one line in `~/.config/corne-arcane/vial`:

```bash
mkdir -p ~/.config/corne-arcane
echo ~/Applications/Vial.AppImage > ~/.config/corne-arcane/vial
```

The file works for menu launches too. `CORNE_ARCANE_VIAL_BIN` overrides it, but
a shell export does not reach the application menu; set it in
`~/.config/environment.d/` if you want it there. If Vial cannot be found, the
launcher stops before pausing the daemon and says so in a desktop
notification. Nix pins this automatically. `CORNE_ARCANE_SYSTEMCTL`, `CORNE_ARCANE_SERVICE`, and
`CORNE_ARCANE_KWIN_SCRIPT` already default correctly on Debian.

The units start with `graphical-session.target`, which GNOME and Plasma reach
and XFCE, Cinnamon and i3 do not. For those, the package installs
`/etc/xdg/autostart/corne-arcane.desktop`, which starts whichever Corne Arcane
units you enabled when you log in. XFCE and Cinnamon run it, and so does i3
when its config runs `dex --autostart` (Debian's default i3 config does). For
an i3 config without that line, or a window manager that ignores autostart,
add this to `~/.config/i3/config` or its startup file:

```
exec --no-startup-id systemctl --user start corne-arcane-host.service
```

## Focus producers

Focus semantics need something to report the active window. Without a producer
everything else still works and focus simply stays at its default.

- KWin: loaded automatically, and requires Plasma 6. The script uses
  `workspace.windowActivated`, which Plasma 5 does not provide.
- GNOME Shell: opt-in, and requires Shell 45 or newer. The extension uses the
  ESM extension API, which GNOME 44 and older cannot load at all.
- Plain X11: enable `corne-arcane-focus-x11.service`, which needs `xprop` from
  `x11-utils` (`services.corne-arcane-host.x11FocusProducer = true;` on NixOS). Use this on XFCE, Cinnamon, i3, or Plasma 5. It reports the
  `WM_CLASS` pair and `_GTK_APPLICATION_ID`, which between them cover the
  spellings the profile table knows.
- Other Wayland compositors have no producer yet.

An application nothing recognizes still works; it just presents as the default
scene. To find out which ones those are, run the producer in the foreground and
use the desktop normally:

```bash
corne-arcane-focus-x11 --verbose      # prints each identity and what it matched
```

Anything printed as `UNMATCHED` is a missing alias in
`arcane_host/profiles.py`. Profiles draw only from the Scene and Floor values
the firmware already knows, and never `Scene.FOCUS` or `Floor.SPECIAL`, which
belong to the Pomodoro ritual. That leaves nine pairs, of which seven are taken;
`Scene.REVEL` is the last value the enum can hold, because the split snapshot
gives scene exactly two bits. A new category therefore either shares a pair with
an existing profile or is firmware work.

Matching also decides how a profile competes with background media: something
playing supplies `Scene.ARCHIVE` only while nothing is recognised, so a
recognised window keeps its own district with music on.

## Optional adapters

Nothing below is auto-enabled by the package.

- Zsh: source `share/corne-arcane/zsh/corne-arcane.zsh`.
- Bash: source `share/corne-arcane/bash/corne-arcane.bash`.
- Fish: source or link
  `share/corne-arcane/fish/conf.d/corne-arcane.fish` from Fish's `conf.d`.
- GNOME: explicitly install or link
  `share/gnome-shell/extensions/corne-arcane-focus@griffinhale.github.io`, then
  enable that UUID through GNOME Extensions.
- Firefox: load the extension under `share/corne-arcane/firefox`; see
  [Firefox](#firefox) below.

Shell hooks report only monotonic duration, integer status, and normalized
repository state. GNOME reports only application/desktop identifiers, and the
X11 producer reads only `WM_CLASS`; neither can reach a window title. Firefox
sends exactly event kind and intensity; it never reads or sends URLs, titles,
content, history, forms, referrers, or typed text. An absent bus, denied
permission, missing native host, or extension restart disables only that
adapter.

### Firefox

The Firefox bridge has two parts. The extension watches for scrolls, tab
switches and page loads. The native host, `corne-arcane-browser-bridge`, passes
them to the daemon. The package installs the host and its manifest. You load
the extension yourself.

A copy signed by Mozilla stays installed across restarts in any Firefox:

1. Download `corne-arcane-activity-1.0.0.xpi` from the
   [releases page](https://github.com/Griffinhale/corne-arcane-oled/releases).
2. Open it in Firefox (drag it onto a window, or **File > Open File**) and
   click **Add**.
3. Switch tabs once. The extension starts the native host on the first event,
   so this should now print a process:

   ```bash
   pgrep -af corne-arcane-browser-bridge
   ```

The signed copy is version 1.0.0 of the files under `host/firefox`. To run a
changed copy instead, load it as a temporary add-on. Ordinary Firefox releases
only keep signed extensions, so **Firefox drops a temporary add-on at every
restart** and you load it again each time. Firefox ESR, Developer Edition and
Nightly can keep it if you set `xpinstall.signatures.required` to `false` in
`about:config`.

1. Find the extension folder:

   ```bash
   echo "$(dirname "$(readlink -f "$(command -v corne-arcane-browser-bridge)")")/../share/corne-arcane/firefox"
   ```

2. Open `about:debugging#/runtime/this-firefox`, click **Load Temporary
   Add-on...**, and pick `manifest.json` in that folder.
3. Switch tabs once. The extension starts the native host on the first event,
   so this should now print a process:

   ```bash
   pgrep -af corne-arcane-browser-bridge
   ```

Firefox finds the host through its manifest,
`io.github.griffinhale.corne_arcane.json`. The Debian package and
`make -C host install PREFIX=/usr` put it in
`/usr/lib/mozilla/native-messaging-hosts`, where Firefox looks. On NixOS, set
`services.corne-arcane-host.firefoxBridge = true;`. That hands the host to
the Firefox wrapper, so it needs `programs.firefox.enable = true;` too. A
Firefox you installed another way will not see it.

Snap and Flatpak builds of Firefox may not see hosts under `/usr/lib/mozilla`
at all. That is untested.

## Stop and uninstall

Stop the services first, in your own session:

```bash
systemctl --user disable --now corne-arcane-host.service corne-arcane-focus-x11.service \
  corne-arcane-tray.service
```

Then undo whatever you turned on from the list above:

- Shell hooks: delete the line that sources `corne-arcane.zsh` or
  `corne-arcane.bash` from `~/.zshrc` or `~/.bashrc`, or the Fish `conf.d`
  link. Do this before removing the package, or every new shell reports a
  missing file.
- GNOME: `gnome-extensions disable corne-arcane-focus@griffinhale.github.io`,
  and remove the copy or link under `~/.local/share/gnome-shell/extensions/`
  if you made one.
- Firefox: a temporary add-on is gone after a restart. If you kept it, remove
  it from `about:addons`.
- Vial location: `rm -r ~/.config/corne-arcane` if you created it.

Finally remove the files, the same way you installed them:

```bash
sudo apt remove corne-arcane-host            # Debian package
sudo make -C host uninstall PREFIX=/usr      # make install, same PREFIX
```

On NixOS, drop the `corne.nix` import and rebuild.

## Desktop city window

A window that shows the city on a machine whose keyboard has no displays. It
covers the screenless keyboard and both non-split cases at once, and is the
reference implementation any later platform can read.

On the keyboard, key positions never leave the firmware, and sampling them on
a desktop is exactly the access this project refuses, so the window reads no
input at all. The tower, its floor, the sky, the resident, and whatever the
notification summary sends walking through are derived from the same bounded
enums the daemon already sends over Raw HID.

The champions duel anyway. With no hands at the keys the world would stand
still, so the window runs the firmware's own simulation driven by a caster
that fabricates its own key positions from a seeded generator: real chains,
compiled by the real incantation compiler, from invented input. A seed replays
a city exactly, and the world never runs down -- a felled champion is carried
off and the roster walks a replacement in. `--no-duels` stills them and leaves
only the host's semantics moving.

The renderer lives in `desktop/`, which is the desktop product: its own
drawing layers and autonomous world, compiled natively over the simulation the
firmware also compiles. None of it is flashed. QMK compiles the explicit list
in `firmware/rules.mk`, the dependency runs one way only (`desktop` reads
`firmware/sim`, never the reverse), and `make hygiene` fails if either stops
being true, so the desktop costs the firmware image nothing. The packages build
it and install it as `corne-arcane`, with a menu entry:

```bash
corne-arcane            # follow the running service
corne-arcane --tour     # walk the districts: no service, no bus, no keyboard
```

From a checkout, run `make city-lib` at the repository root, then
`python3 -m arcane_host.city_window` in `host/`.

By default the window is one continuous scene. The three columns between the
two towers are world the panels cannot show -- the battlefield axis crosses
them and nothing is ever drawn there -- so on a desktop they are unlit rather
than desk-coloured, and the keyboard's two-panel framing disappears.

- `--layout city` one scene, the default
- `--layout desk` two panels with the desk between them, as the review sheets
  and the hardware show it
- `--layout left`, `--layout right` a single tower
- `--layout town` a 256x256 city: one wizard tower at the centre, cut away to
  the storey the host is on, houses and hills either side, a paved plaza in
  front, and the hour, the weather and the duel in the sky
- `--size 512x512` a fixed window with the city centred at the largest whole
  pixel scale that fits; `--scale N` instead sizes the window to the city
- `--no-duels` still champions; `--seed` chooses which city you get

With no scale asked for, each layout takes the largest whole-pixel scale that
keeps the window about 512 tall, so the panels come up at 4x and the town at
2x without a flag.

The first four layouts are the same pixels reframed. Every coordinate behind
them is written against a 32x128 canvas, so 67x128 is all they can show and a
squarer window letterboxes rather than revealing more city.

`town` is a second drawing layer rather than a reframing, on a square canvas of
its own. It reads the same projection -- the floor decides which storey is lit,
the sky phase decides the hour, the civic clock paces the residents crossing
the plaza, and a spell in flight is the same spell, arcing out over the roofs
instead of across a desk. It shares the world, not the pixels.

The app is a client of `corne-arcane-host.service`, never a second daemon. It
reads the service's Control interface (below) for the world the keyboard is
being sent and for the keyboard link, which it names under the image:
connected, no keyboard found, lent to Vial, and so on. It never opens the
keyboard. With the service stopped, the city is drawn offline and the line
says the service is not running.

Set `CORNE_ARCANE_CITY_LIB` to load the shared library from somewhere else.

### Control interface

The service exports `io.github.Griffinhale.CorneArcane.Control` at
`/io/github/Griffinhale/CorneArcane` on the session bus:

- `Status() -> (link, device, paused, owner)`. `link` is one of `starting`,
  `connected`, `absent`, `denied`, `several`, `failed` or `paused`.
- `Pause(owner)` closes the keyboard so another tool can open it, and
  `Resume()` takes it back. A pause belongs to the caller's bus connection and
  ends when that connection closes. So a one-off `gdbus call ... Pause` gives
  the keyboard back as soon as `gdbus` exits; hold a pause from a running
  client. A second `Pause` fails with `Control.Busy`.
- `World() -> (yyyyyyyy)`: the eight bytes the keyboard is being sent (scene,
  notification count, category, priority, age, persistent, civic, secondary).
  They are small integers only; no title, path or message text exists at this
  level.
- Signals `StatusChanged` and `WorldChanged` carry the same values when they
  change.

## Tray icon

`corne-arcane-tray` puts the keyboard link in the panel. The icon shows a
keyboard while the link is up, a pause sign while a tool has the keyboard, and
a warning when there is no keyboard or no service. Its menu has Pause keyboard
(held for as long as the tray runs), Open Vial and Open Corne Arcane; a click on
the icon opens the app too. It is the app's own control layer, so the app and
the tray never disagree about who has the keyboard.

Turn it on for your session:

```bash
systemctl --user enable --now corne-arcane-tray.service
```

On NixOS set `services.corne-arcane-host.trayIcon = true;` instead.

The icon is a StatusNotifierItem. KDE Plasma, XFCE, Cinnamon and most other
panels show it as it is. **GNOME Shell does not show tray icons by itself**:
install the AppIndicator extension first (`gnome-shell-extension-appindicator`
on Debian and Ubuntu, `gnomeExtensions.appindicator` on NixOS) and enable it in
Extensions.

## Tasks

- Run host tests: `./run_tests.sh`
- Show the city with no keyboard: `corne-arcane --tour`
- Exercise one offline exchange: `python -m arcane_host.daemon --dry-run --once --session 1`
- Check the Debian layout: `make install DESTDIR=/tmp/stage PREFIX=/usr`
- Watch live metrics from the keyboard: `corne-arcane-diagnostics --observe 300 --json`
- Launch Vial safely: `corne-arcane-vial`

Diagnostics stop and later restore an active host service for a query or
observation window. They leave an inactive service inactive and fail if its
state cannot be determined. `--no-service-handoff` bypasses that protection
only for deliberate development use.

The architecture is documented in
[`../docs/architecture.md`](../docs/architecture.md); build and test conventions
are in [`../docs/development.md`](../docs/development.md).
