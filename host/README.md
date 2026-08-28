# Corne Arcane host package

The optional host package sends bounded, privacy-redacted desktop and browser
activity semantics to `griffin_arcane`. Firmware remains fully functional when
it is absent.

Public commands and identities remain `corne-arcane-host`,
`corne-arcane-event`, `corne-arcane-diagnostics`, `corne-arcane-vial`,
`io.github.Griffinhale.CorneArcane`, and `corne-arcane-host.service`.
`corne-arcane-focus-x11` is new and opt-in.

Raw HID v3 retains the 32-byte report/eight-byte payload and adds secondary
activity values for scroll, tab selection, and page events. The generic method
is usable without Firefox:

```bash
corne-arcane-event browser scroll 1
```

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
producer's unit ships but is not declared without it.

On Debian or Ubuntu, build the package from a checkout:

```bash
sudo apt install devscripts debhelper python3-gi dbus-user-session
dpkg-buildpackage -b -uc -us
sudo apt install ../corne-arcane-host_*.deb
systemctl --user enable --now corne-arcane-host.service
```

`dbus-user-session` is required, not optional: the daemon is a D-Bus-activated
user service and the diagnostics and Vial handoff both use `systemctl --user`.
It is present by default on desktop installs and absent on minimal ones.

To install without building a package, `make install PREFIX=/usr` places the
same layout directly. Unplug and replug the keyboard afterwards so the udev
rule applies.

Debian has no `vial` package -- Vial ships as an AppImage -- so set
`CORNE_ARCANE_VIAL_BIN` to its path before using `corne-arcane-vial`. Nix pins
this automatically. `CORNE_ARCANE_SYSTEMCTL`, `CORNE_ARCANE_SERVICE`, and
`CORNE_ARCANE_KWIN_SCRIPT` already default correctly on Debian.

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
- Firefox: explicitly install the extension assets under
  `share/corne-arcane/firefox`. The packaged native-messaging manifest invokes
  `corne-arcane-browser-bridge`.

Shell hooks report only monotonic duration, integer status, and normalized
repository state. GNOME reports only application/desktop identifiers, and the
X11 producer reads only `WM_CLASS`; neither can reach a window title. Firefox
sends exactly event kind and intensity; it never reads or sends URLs, titles,
content, history, forms, referrers, or typed text. An absent bus, denied
permission, missing native host, or extension restart disables only that
adapter.

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
being true, so the desktop costs the firmware image nothing. It is not packaged
yet, so the window runs from a checkout:

```bash
make city-lib                                  # from the repository root
cd host
python3 -m arcane_host.city_window --tour      # no daemon, no bus, no keyboard
python3 -m arcane_host.city_window             # follow the live daemon
```

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

The live mode is the daemon: it builds the same semantic stack in the same
process, so nothing new consumes the shared Raw HID interface. It also claims
the same bus name, so stop `corne-arcane-host.service` first or focus reporting
will go to the running service instead. Arguments the window does not recognise
are handed to the daemon unchanged, so `--scale 5 --verbose` works.

Set `CORNE_ARCANE_CITY_LIB` to load the shared library from somewhere else.

## Tasks

- Run host tests: `./run_tests.sh`
- Show the city with no keyboard: `python -m arcane_host.city_window --tour`
- Exercise one offline exchange: `python -m arcane_host.daemon --dry-run --once --session 1`
- Check the Debian layout: `make install DESTDIR=/tmp/stage PREFIX=/usr`
- Observe physical acceptance metrics: `corne-arcane-diagnostics --observe 300 --json`
- Launch Vial safely: `corne-arcane-vial`

Diagnostics stop and later restore an active host service for a query or
observation window. They leave an inactive service inactive and fail if its
state cannot be determined. `--no-service-handoff` bypasses that protection
only for deliberate development use.

The architecture is documented in
[`../docs/architecture.md`](../docs/architecture.md); build and test conventions
are in [`../docs/development.md`](../docs/development.md).
