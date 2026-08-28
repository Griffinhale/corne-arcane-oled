# The Apple shell

The third shell, after the desktop window and the browser. It shows the
**city**, never the duel, for the reason the other two do: on the keyboard key
positions never leave the firmware, a phone has no keyboard to read, and
nothing here samples input. The world plays itself.

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
repository as a local Swift package. The targets contain only
`apple/App/CityApp.swift` and `apple/Widget/CityWidget.swift`, respectively;
all shared implementation stays in CityKit.

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
  shell. Without it a shell that renders once a quarter hour draws an impact
  burst for something that happened fourteen minutes ago, every single time.
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

## Host semantics

There is no daemon and no focused window to read, so the app is self-playing,
exactly like the browser shell. The one honest way to give it host semantics is
a Focus mode: a Focus is a bounded enum, so it maps onto `DUEL_CIVIC_MODE`
(normal / quiet / urgent) and can pick a floor without any text, URL, window
title or notification body crossing the boundary. Never sample keystrokes or
read the pasteboard, and do not reach for Screen Time categories -- whatever
goes in has to stay something the firmware would itself accept.
