# The Apple shells

An iPhone app with a Home Screen widget, and an Apple Watch app with
complications, all built on one Swift package (`CityKit`) over the same C the
keyboard runs. They follow the desktop window and the browser. They show the
**city**, never the duel, for the reason the other two do: on the keyboard key
positions never leave the firmware, a phone or watch has no keyboard to read,
and nothing here samples input. The world plays itself.

Read `web/README.md` first. The browser shell is the closest precedent and
almost every decision transfers unchanged.

## What builds where

| | Linux | macOS |
| --- | --- | --- |
| `CityKit` (the renderer, seek, timelines) | yes | yes |
| `city-check` (parity leg, invariants) | yes | yes |
| `CityView`, `CityTimelineProvider` | compiled away | yes |
| the app and the widget targets | no | Xcode |

`CityView.swift` and `WidgetProvider.swift` are wrapped in
`#if canImport(SwiftUI)` / `canImport(WidgetKit)`, so a Linux build sees empty
files and a Mac build type-checks them with a plain `swift build`. Only the two
`@main` declarations under `apple/App` and `apple/Widget` need Xcode, and they
are deliberately the smallest files here.

## Running the gates

On macOS, with the system toolchain:

```sh
make swift-parity
```

On Linux, nixpkgs ships the compiler and SwiftPM but does not wire the corelibs
onto the runtime path, and SwiftPM drives `cc`, which in a plain shell is gcc
and does not understand `-fblocks`. Both are handled by:

```sh
nix-shell apple/shell.nix --run "make swift-parity"
```

That renders `web/tools/parity_matrix.json` twice -- once through the desktop
library and its own binding, once through this package -- and diffs the two
hash files, then runs the invariants. The matrix, the line format and the diff
are the WASM leg's, because a leg that checked something slightly different
would be a second opinion rather than a third witness.

`swift test` is deliberately absent. The other two legs are programs --
`parity_native.py` and `parity_wasm.mjs` -- and so is the firmware's own
acceptance rig; a leg that needs a test framework to run is a leg that does not
run everywhere the shell does. It also does not run at all under the nixpkgs
Swift, which ships no `libIndexStore.so` for SwiftPM's test discovery.

## Wiring the Xcode targets

Open `apple/CorneArcane.xcodeproj`. Its `CorneArcane` app target embeds the
`CityWidgetExtension` target, and both link the `CityKit` product from this
repository as a local Swift package. The target entry points are
`apple/App/CityApp.swift` and `apple/Widget/CityWidget.swift`; all shared
implementation stays in CityKit. `apple/App/ChargingCityView.swift` is the one
device-only policy surface.

The focused watch experiment is the separate `CorneArcaneWatch` scheme. Its
watch-only host app embeds `CorneArcaneWatchWidgetExtension`, and both targets
link CityKit. The complication and Smart Stack remain static timeline surfaces;
opening the host launches directly into the continuously animated city. No
signing team is stored in the project.

## Full watch app

The host presents a 162x197 source-pixel crop from `.town`, matched to the
logical-point shape of the Apple Watch SE 3 40 mm display. It keeps the full
central tower, wizard, ward, spell lane, and neighbouring roofs without
squeezing the square town into a portrait screen. SwiftUI scales that crop with
nearest-neighbour interpolation and a responsive fill, so the same composition
also fills larger watches without hard-coded view dimensions.

Seed `0x5A` runs from local midnight, exactly like the complication. A 10 fps
presentation timer only wakes the driver; each frame phase comes from the
current whole 40 ms world tick. The driver reuses one `City`, seeks across time
missed while inactive, and performs long cold replays on a serial actor so the
main actor remains responsive. Those replays cooperatively stop before their
short renderer warm-up when lifecycle cancellation arrives. An immediate
renderer-generated still covers that first replay. The driver stops the timer
while backgrounded, inactive, or in reduced luminance; the last readable frame
remains on screen in those states.

The crop was selected from actual-size deterministic candidates rather than
from renderer coordinates alone. Regenerate the 40 mm comparisons and an
active-spell sequence with:

```sh
swift run -c release city-check watch-app-candidates /tmp/corne-watch-app-candidates
```

## Watch complication experiment

The watch extension supports only `.accessoryRectangular` and
`.accessoryCircular`. Both render seed `0x5A` from local midnight on the same
five-minute cadence as the iOS widget. There is no App Group, shared epoch,
WatchConnectivity, configuration intent, or cross-device state.

Neither family scales the full town. Rectangular uses a 148x72 source-pixel
window from the landscape renderer around the roof, lit study, balcony wizard,
and spell lane. Circular uses a 64x64 town window centred on the tower-window
motif. CityKit converts black pixels to transparency for these compositions so
watch faces can tint the white ink without acquiring an opaque black card.

The widget source contains deterministic Xcode previews for both families in
full-colour and accented modes. A matching actual-size capture set (150x64 and
58x58 points at @2x) can be generated on macOS with:

```sh
swift run -c release city-check watch-captures /tmp/corne-watch-captures
```

With a watchOS simulator runtime installed, build the `CorneArcaneWatch`
scheme for an available Apple Watch destination. A target-level SDK compile,
which is useful even when no watch runtime is installed, is:

```sh
xcodebuild -project apple/CorneArcane.xcodeproj \
  -target CorneArcaneWatch -sdk watchsimulator -configuration Debug \
  CODE_SIGNING_ALLOWED=NO build
```

Simulator builds sign locally. For a device or archive, choose a development
team for both targets in Xcode; no machine-specific team identifier is stored
in the project.

Device arm64 and Apple Silicon simulator arm64 are the same code. Nothing in
`firmware/sim` is endian- or width-sensitive and there are no floats anywhere.
Do not carry `-nostdlib` over from the web build: iOS has a full libc, so
`memcpy` and `memset` come from the system and `duel_wasm.c`'s hand-written
pair are not wanted.

## Things that will bite

- **The input struct goes through the firmware's acceptance path** and is
  refused with `DUEL_CITY_ERR_INPUT` if any field is out of range. `City.init`
  starts from `duel_city_tour_stop`, which is valid by construction. Hand-zero
  it and nothing draws.
- **The world takes its first tick at time zero.** `advance(0)` runs a tick, so
  world time zero has already lived one. `City.init` does this. The browser and
  the desktop agree, and a shared link only matches if this shell agrees too.
- **Use the accumulator.** `CityDriver` derives world time from elapsed wall
  time and floors it to whole ticks. The world's cadence is 40 ms and the
  display's is whatever ProMotion is doing.
- **A backgrounded app resynchronises rather than replaying.** Catch-up is
  capped at five ticks, so the world jumps. That is the keyboard's own
  behaviour across a USB suspend, it is deliberate, and it should not be fixed
  here. It is why a share link takes its position from the world's tick count
  and not from the wall clock.
- **Seek replays tick by tick and renders a run-up.** How long the run-up is
  belongs to the renderer -- `duel_city_seek_warm_frames`, introduced in ABI 6 -- not to this
  shell. Without it a shell that renders once every five minutes draws an impact
  burst for something that happened minutes ago, every single time.
- **Render at scale 1 and magnify in the view layer.** `.interpolation(.none)`
  is the browser's `image-rendering: pixelated`. `duel_city_fit_scale(TOWN,
  390, 844)` returns 1 on a phone in logical points anyway.

## Landscape

ABI 7 adds `LANDSCAPE`, a 400x240 drawing layer with more world on either side
of the tower. It is not a stretched or cropped `TOWN`: the sky, hills, streets,
plaza, residents and spell lanes are composed across the wider surface. The
widget asks for it only in `systemMedium`; `systemSmall` and `systemLarge` keep
the 256x256 town. At the renderer's default scale of 2 it is exactly 800x480,
which also fits the 7.5" e-ink panel the layout was sized for.

The app uses that wide composition only when the phone is both externally
powered and resting in landscape. In that ambient mode it hides system chrome
and keeps the display awake; unplugging or returning to portrait restores the
ordinary square town and the normal idle timer. Pass
`--charging-landscape-preview` as a Debug launch argument to exercise the mode
in Simulator without pretending its battery state changed.

## Host semantics

There is no daemon and no focused window to read, so the app is self-playing,
exactly like the browser shell. The one honest way to give it host semantics is
a Focus mode: a Focus is a bounded enum, so it maps onto `DUEL_CIVIC_MODE`
(normal / quiet / urgent) and can pick a floor without any text, URL, window
title or notification body crossing the boundary. Never sample keystrokes or
read the pasteboard, and do not reach for Screen Time categories -- whatever
goes in has to stay something the firmware would itself accept.
