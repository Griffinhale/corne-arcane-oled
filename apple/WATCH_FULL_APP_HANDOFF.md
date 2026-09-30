# Corne Arcane full watch app handoff

## Objective

Build the foreground watchOS experience that the complication experiment now
motivates: an animated, monochrome Corne Arcane city composed specifically for
the Apple Watch SE 3 40 mm display.

The complication and Smart Stack widget are already successful. Keep them
glanceable, static timeline surfaces. The new work belongs in the watch app
host and must not turn the WidgetKit extension into an animation mechanism.

## Proven baseline

- `CorneArcaneWatch` is a watch-only host app.
- `CorneArcaneWatchWidgetExtension` uses CityKit and supports only
  `.accessoryRectangular` and `.accessoryCircular`.
- Both families render purpose-designed source-pixel crops rather than a
  miniature 256x256 town.
- The widget uses seed `0x5A`, the existing local-midnight world anchor, and
  deterministic five-minute timeline entries.
- The extension renders as an alpha-bearing template and behaves correctly in
  full-color and accented/tinted contexts.
- The app and extension are both picker-facing as **Corne Arcane**. This naming
  matters: the first physical build used `City Complication` / `The city`, which
  made the working complication difficult to find.
- The signed build installed and launched on a physical Apple Watch SE 3,
  watchOS 26.3. The user confirmed the snapshot in Smart Stack and successfully
  added the complication to a watch face.
- The host is intentionally static today and only explains how to add the city.

Existing verification is green:

```text
PASS parity: 4320 frames byte-identical, native and Swift
PASS city-check: the third shell holds what the other two hold
```

All circular and rectangular composition/legibility invariants pass.

## Relevant files

- `WatchApp/CorneArcaneWatchApp.swift` — replace the instructional host UI here.
- `WatchWidget/CorneArcaneWatchWidget.swift` — proven complication; avoid
  changing it unless a shared presentation primitive is genuinely improved.
- `Sources/CityKit/City.swift` — renderer wrapper and the canonical 40 ms world
  tick.
- `Sources/CityKit/CityView.swift` — existing animated Apple-platform driver;
  useful reference, but its square `.fit` presentation and launch-relative
  clock are not the finished watch design.
- `Sources/CityKit/Timeline.swift` — `CityFrame`, `CityPixelRect`, cropping, and
  CGImage conversion.
- `Sources/city-check/main.swift` — parity, composition invariants, and watch
  capture generation.
- `CorneArcane.xcodeproj/project.pbxproj` and
  `xcshareddata/xcschemes/CorneArcaneWatch.xcscheme` — existing targets/scheme.

`App/ChargingCityView.swift` is UIKit-specific and must not enter the watch
target.

## Product direction

The full app should be one immersive, self-playing city surface:

- No navigation hierarchy, Digital Crown interaction, settings, notifications,
  WatchConnectivity, App Intents, or configuration UI in this phase.
- Open directly into the animated city.
- Use a portrait, source-pixel viewport designed for the 40 mm SE 3. Do not
  display the square town with `.aspectRatio(.fit)` and accept letterboxing.
- Keep important silhouettes away from rounded corners and system overlays.
  Start with the central tower/duel as the visual anchor, then include enough
  surrounding roofs and spell lane that the place still reads as a city.
- Render at scale 1, enlarge in SwiftUI with `.interpolation(.none)`, and use a
  black ground with monochrome city pixels.
- Make layout responsive with `GeometryReader`; do not hard-code the physical
  device resolution into view layout. Select the crop in renderer source pixels
  and verify its safe region on the actual 40 mm display.

An initial crop should be chosen by generating candidate source-pixel captures,
not by guessing in the final view. A roughly 0.82:1 portrait crop from `.town`
is a useful starting shape, but its exact rectangle is an experiment and must
be visually validated.

## Time and animation semantics

Preserve seed `0x5A` and the local-midnight world anchor so tapping the
complication opens the same deterministic world rather than a new city that
starts at app launch.

Implement a watch-specific driver or carefully deepen `CityDriver`:

1. On activation, create the city and seek to the current whole 40 ms tick
   since local midnight.
2. While active, derive every rendered frame from elapsed wall time floored to
   `City.frameIntervalMs`; never count timer callbacks.
3. A 10–25 fps presentation can sample the 40 ms world without changing it.
   Begin at a conservative cadence and measure on the physical SE 3. At 10 fps,
   use `now / City.frameIntervalMs` as the frame phase so skipped presentation
   frames do not cause drift.
4. Stop timers when inactive/backgrounded. On resume, seek forward to the
   deterministic current time rather than replaying presentation callbacks.
5. Respect reduced-luminance / Always On state. Freeze the last readable frame
   or update very infrequently; do not keep the foreground animation cadence
   when the display is dimmed.

Do not change the C renderer to make watch layout easier. Presentation cropping
belongs after rendering, as it does for the complications.

## Suggested structure

Keep the watch-specific policy out of the portable renderer:

```text
WatchApp/
  CorneArcaneWatchApp.swift   app entry and scene phase
  WatchCityDriver.swift       local-midnight clock, lifecycle, render cadence
  WatchCityView.swift         portrait viewport and watch presentation
```

The driver should publish a cropped `CGImage` or a small presentation value. It
should own one `City` instance and reuse its buffers; avoid allocating a second
renderer every timer fire. Keep UI state on the main actor and measure whether
initial deterministic seeking needs to occur off the main thread before adding
concurrency complexity.

If `CityDriver` is generalized instead, its interface must make clock origin,
viewport, and cadence explicit while preserving existing iOS behavior by
default. Do not silently change the iPhone app's launch-relative animation.

## Acceptance criteria

- Launches directly into a visibly animated city on Apple Watch SE 3 40 mm.
- Central tower, wizard/ward, and at least one surrounding city cue remain
  legible at arm's length; the screen does not read as noisy white pixels.
- The composition fills the portrait display intentionally without stretching,
  smoothing, or obvious square-view letterboxing.
- Foreground motion is smooth enough for the pixel-art cadence and does not
  drift from deterministic world time.
- Backgrounding and reopening land at the correct local-midnight world time.
- Reduced-luminance / inactive states stop or sharply reduce work.
- VoiceOver exposes one useful stable label rather than announcing every frame.
- Existing circular/rectangular complications and Smart Stack behavior remain
  unchanged and discoverable as Corne Arcane.
- No signing team identifier is saved in the project.

## Verification loop

Run:

```sh
make swift-parity
swift run -c release city-check watch-captures /tmp/corne-watch-captures
xcodebuild -project apple/CorneArcane.xcodeproj \
  -scheme CorneArcaneWatch -configuration Debug \
  -destination 'generic/platform=watchOS' \
  -derivedDataPath /tmp/corne-watch-device-derived \
  CODE_SIGNING_ALLOWED=NO build
```

For physical-device verification, use the signed-in development team only as a
temporary Xcode or command-line override. Install and run on the available
Apple Watch SE 3 40 mm. Do not persist a personal team in the project.

Capture and inspect at least:

- Initial foreground frame at actual 40 mm size.
- The same view during an active spell/ward moment.
- A reduced-luminance or inactive-state frame.
- Reopen after backgrounding, confirming deterministic time continuity.
- The existing rectangular and circular complications after the app update.

Also build one 44 mm simulator destination to catch accidental 40 mm
hard-coding, but treat the physical 40 mm SE 3 as the design target. The
watchOS 26.2 simulator previously suffered runtime/migration instability, so a
simulator-only result is not sufficient.

## Known limitations and decisions

- WidgetKit snapshots do not animate. Their five-minute entries are correct;
  watchOS decides the exact display refresh opportunity.
- The physical complication was user-verified, but Xcode's device screenshot
  path was unreliable while it attempted to copy shared-cache symbols. Retake
  representative physical screenshots during the full-app work.
- No cross-device synchronization, shared epoch persistence, or App Group was
  added. Keep that out of this phase unless separately requested.
- There is no evidence yet that navigation or interaction improves the city.
  The full app is justified specifically as the place where continuous motion
  can exist.

## Worktree caution

The worktree already contains unrelated and preceding iOS/widget work. Preserve
all existing modifications and untracked files. Do not reset, clean, overwrite,
or discard them while implementing the watch app.
