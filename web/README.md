# The browser shell

The city in a tab. The same C the keyboard runs, compiled to `wasm32`, drawn
on a canvas by 250 lines of JavaScript -- 110 to load the module and convert
its pixels, 140 for the page around it.

There is no daemon, no server, no origin and no permission prompt, because the
world self-plays: the champions fabricate their own moves from a seed, so there
is nothing to sample and nothing to ask for. A canvas, a clock and a seed is
the whole shell.

## Building

```bash
make web-lib      # from the repository root, or `make` in here
make web-parity   # the acceptance gate: native and WASM, byte for byte
```

Needs a clang with the `wasm32` target plus `wasm-ld`; the parity harness also
needs Node and Python. On a distribution whose clang is wrapped for the host
target, use the unwrapped one -- the wrapper injects host linker flags that
`wasm-ld` rejects:

```bash
nix shell nixpkgs#llvmPackages.clang-unwrapped nixpkgs#lld nixpkgs#nodejs
```

To look at it, serve the directory over HTTP and open `index.html`. Any static
file server will do; `file://` works too, with a fallback path in the loader
for the missing MIME type.

`make web-parity` runs two gates:

- **parity** -- every layout, three seeds, 240 frames each, rendered by the
  WebAssembly module and by the native library the desktop window loads. All
  3 600 frames must agree byte for byte. Both sides drive the self-playing
  world, so a divergence in the simulation shows up as well as one in the
  renderer.
- **share** -- arriving at a moment by link must equal having watched the world
  into it. This is the URL's promise, and it is a separate code path
  (`City.seek`) from the one a tab left open takes (`City.advance`).

## The URL is the product

`?seed=&t=&view=&floor=` are the arguments of a pure function: the same four
produce the same pixels on any machine, which is what makes a link worth
sending. `paused=1` arrives stopped on that frame.

Two details are load-bearing and easy to get wrong:

- **`seek` replays tick by tick.** It cannot be one long `advance`, because
  `duel_ambient_advance` resynchronises across a gap longer than five ticks --
  so a single jump would land in a world that had skipped its own history. A
  simulated hour is ~90 000 ticks and replays in about 22 ms, which is why the
  honest implementation is also the affordable one.
- **`seek` renders a run-up, not just the world.** The renderer carries two
  policies between frames: the floor transition, which is what makes the tower
  slide between storeys instead of snapping, and the outcome flash, which arms
  on a sequence number it has not seen before. Arriving without that history
  draws the slide from a standing start -- measured at 29 pixels of 8576 --
  and bursts for whatever last happened, however long ago that was. How long
  the run-up has to be belongs to the renderer rather than to this shell:
  `duel_city_seek_warm_frames`, twenty-five frames, asked once at load. It is
  rendered through the cheapest layout, costs about a millisecond, and makes
  the arrival frame exact.

## Deploying

The site is a container: a build stage compiles the module with clang, and
`nginx:alpine` serves the handful of static files. There is nothing to
configure at runtime -- no database, no environment, no secrets -- because
there is no server side.

```bash
docker build -t duel-city .          # from the repository root
docker run --rm -p 8080:80 duel-city # http://localhost:8080
```

The `Dockerfile` lives at the repository root rather than in here, because the
browser shell is a shell *over* `firmware/sim` and `desktop/`, so the build
context has to include them. It builds only the browser shell; the keyboard
image is QMK's business and never touches it.

Deployment is a `git push` to a [Dokku](https://dokku.com) remote, which builds
the `Dockerfile` and reloads its proxy. Host, app name and domain are
deployment facts rather than repository facts, and this repository is public,
so they are not written down here.

Three details in `nginx.conf` are worth knowing before editing it, because each
one fails silently rather than loudly:

- **Nothing is cached immutably.** No filename carries a content hash, so a
  redeploy that changed `duel_city.wasm` while a browser held an old copy would
  run a different world from the one the URL names. Everything revalidates and
  is answered with a 304.
- **A `types` block replaces the MIME map, it does not extend it.** Adding one
  to be explicit about `.wasm` cost every other file its type, and `index.html`
  came back as `application/octet-stream`, which a browser downloads instead of
  rendering. The base `mime.types` has mapped `application/wasm` since nginx
  1.21.4, so it is inherited rather than restated.
- **`add_header` is inherited into a location only if that location declares no
  `add_header` of its own.** A single `Cache-Control` inside `location /` was
  enough to drop the CSP and every hardening header from every response the
  site actually serves. They all live at server level together.

The Content-Security-Policy is nearly empty, which is the point: the page has
no CDN, no font host, no analytics and no third party of any kind, so a policy
that describes it honestly forbids almost everything. `'wasm-unsafe-eval'` is
the one entry that looks alarming and is not -- it is the narrow permission to
compile WebAssembly, which Chrome requires before it will instantiate the
module, and it does not permit `eval()`.

## Why standalone clang rather than emscripten

The core is genuinely freestanding, and that is worth demonstrating rather than
hiding. Every `#include <...>` under `firmware/sim` and `desktop` is one of
`stdint.h`, `stdbool.h`, `stddef.h`, `string.h`; there are no floats, no
`malloc`, no `printf`, no `time`, no `rand`, and no non-const file-scope
statics in `firmware/sim` at all.

So it compiles with `-nostdlib` and exactly two symbols supplied by hand,
`memcpy` and `memset`, both at the top of `duel_wasm.c`. Emscripten would
provide a libc and a JavaScript runtime that nothing here calls, and would
obscure the fact that nothing here calls them.

Two numbers make the case:

| | |
| --- | --- |
| module size, `-Oz`, stripped | 78 kB |
| imports the module declares | **none** |

The empty import list is the interesting one. The module cannot call the page,
the network, a clock or storage, because it does not ask for any of them.
`WebAssembly.Module.imports()` returns `[]`, in the console, on the deployed
page. It is not a privacy policy; it is the absence of a mechanism.

## What is in here

| file | |
| --- | --- |
| `duel_wasm.c` | the shim: two libc symbols, the static buffers, the export list |
| `freestanding/string.h` | the two declarations a build with no sysroot still needs |
| `Makefile` | the shared source list plus the shell's own three files |
| `duel-city.js` | loading the module, and the two conversions a browser needs |
| `index.html` `city.css` `city-page.js` | the city |
| `canvases.html` `canvases.css` `canvases-page.js` | one seed, every canvas |
| `favicon.svg` | the tower, cropped out of a real frame by `tools/make_favicon.mjs` |
| `nginx.conf` | how the static files are served |
| `tools/` | the parity gate, and the favicon generator |

## No allocator, and don't add one

Every buffer is a file-scope static in `duel_wasm.c`, sized by what the header
fixes: one `duel_city_state_t`, one `duel_ambient_t`, one `duel_city_input_t`,
and 64 kB of pixels, which is TOWN at scale 1 and covers every other layout.
JavaScript reads them by offset. The module's memory footprint is a
compile-time constant, and there is nothing to free, grow or leak.

`duel_wasm_input_ptr` exposes the input struct so that a future producer could
write the ten bytes directly rather than going through the tour. Neither page
uses it -- they have no semantics to supply -- and it is safe either way,
because whatever is written there still goes through the firmware's acceptance
path on the next render and is rejected if any field is out of range.

## Rendering at scale 1

The library expands to scale internally, which is right for a toolkit that
wants a finished bitmap and wrong for a browser: TOWN at scale 2 costs 262 kB a
frame to produce pixels the compositor produces for nothing. So the browser
renders at scale 1 -- 64 kB a frame for the town, 8.6 kB for the panels --
converts grey to RGBA into one reused `ImageData`, and upscales in CSS with
`image-rendering: pixelated`. Sharp, cheap, and it sidesteps a core change that
the wider port plan lists but this shell does not need.

The *scale* is still the renderer's decision, not the page's:
`duel_city_default_scale` and `duel_city_fit_scale` are exported and asked. ABI
5 moved those rules into C precisely so a second shell would not reimplement
them and drift, and this is the second shell. ABI 6 moved the seek run-up in
after the same argument, and for the same reason.

## Things that will bite

- **The input struct goes through the firmware's own acceptance path** and is
  rejected outright, `DUEL_CITY_ERR_INPUT`, if any field is out of range. Start
  from `duel_city_tour_stop`, which is valid by construction. `duel_wasm_init`
  does this for you. Hand-zeroing the struct and wondering why nothing draws is
  the mistake this paragraph exists to prevent.
- **Use an accumulator** against `duel_city_frame_interval_ms`, not a
  `setInterval`. The world's cadence is 40 ms and the display's is whatever the
  monitor does.
- **The world takes its first tick at time zero.** `advance(0)` runs a tick, so
  world time zero is a world that has lived one, not one that has not started.
  `City.reset` does this, which is what lets the browser and the desktop window
  agree that a seed and a millisecond name one world.
- **`requestAnimationFrame` stops on a hidden tab.** On return
  `duel_ambient_advance` resynchronises rather than replaying -- the catch-up
  cap is 5 ticks -- so the world jumps forward. That is the keyboard's own
  behaviour across a USB suspend, it is deliberate, and it should not be
  "fixed" here. It is why the share link takes its position from the world's
  own tick count rather than from the wall clock: the tick count is the number
  that can be replayed exactly.

## The invariants this shell is held to

- **No input sampling, by any mechanism.** No keyboard, pointer or gamepad
  event reaches the world. The page's controls -- pause, canvas, district --
  are shell UI; the only one that touches `duel_city_input_t` at all is the
  district picker, and it can express nothing but a tour stop the firmware
  itself would accept.
- **Only bounded integer enums cross the boundary.** No text, no bitmaps, no
  framebuffers on a wire.
- **`firmware/sim` is not modified, and `firmware/rules.mk` gains no reference
  to this directory.** `make hygiene` fails if it does. The 622 pre-existing
  golden scenes stay byte identical.
