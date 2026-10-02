# Development workflow

## Toolchain and builds

Native tests require a C11 compiler with ASan/UBSan, Python 3.10 or newer,
Ruff, and clang-format 19-compatible configuration. Exact development-tool
versions are in `requirements-dev.txt`. Firmware builds use the configured
Vial-QMK checkout, `crkbd/rev1`, and `CONVERT_TO=rp2040_ce`.

```bash
make lint            # Ruff, Python/C formatting, and a JavaScript syntax check
make lint-swift      # swift-format, at the version pinned in Makefile
make test            # mechanics, the visual catalog, allocation scan, host tests
make hygiene         # repository conventions (see scripts/hygiene.sh)
make city-lib        # the desktop product's native library (desktop/)
make web-lib         # the browser shell's WebAssembly module (web/)
make web-parity      # native vs WASM, byte for byte -- the browser's gate
make release-build   # release and diagnostic images
make release-budget  # flash, static RAM, hard-stop, and reserve gates
git diff --check
```

With Nix (flakes enabled), one command gives you all of it:

```bash
nix develop                  # every target below except swift-parity
nix develop .#apple          # the Swift shell, for make swift-parity
```

The shell takes the ARM compiler version from `scripts/budget.env` and the
ruff and clang-format versions from `requirements-dev.txt`, and refuses to
start if nixpkgs disagrees with them. It does not fetch Vial-QMK, because
`make release-build` writes into the checkout. Instead it says on entry
whether `QMK_ROOT` (default `~/src/vial-qmk`) is at `VIAL_QMK_REVISION`, and
prints the commands to fix it when it is not. CI does not use this shell.

What each target needs before you run it:

| Target | Needs |
|---|---|
| `make test` | a C11 compiler with ASan/UBSan, GNU make, Python 3.10 or newer. The D-Bus tests also want PyGObject and `dbus-daemon`, and skip without them; each starts its own private bus. |
| `make lint` | Ruff and clang-format at the versions in `requirements-dev.txt`, and Node.js 22 or later |
| `make lint-swift` | swift-format at `SWIFT_FORMAT_VERSION` in `Makefile`, built from that tag with a Swift toolchain |
| `make hygiene` | ripgrep (`rg`) and a git checkout. It fails without either rather than pass. |
| `make city-lib` | a C11 compiler |
| `make web-lib`, `make web-parity` | clang with the `wasm32` target, `wasm-ld`, Node and Python. On Nix, `llvmPackages.clang-unwrapped` (see below). |
| `make swift-parity` | Swift: `nix-shell apple/shell.nix` on Linux, the system Swift on macOS |
| `make lint-swift` | the same Swift, plus swift-format at the version in the Makefile. No distribution ships that exact release, so `make swift-format-tool` builds it from its tag into `.scratch/` once; the Swift shell puts it on `PATH`. |
| `make release-build` | the `qmk` CLI and a Vial-QMK checkout at `VIAL_QMK_REVISION`, found through `QMK_ROOT` |
| `make release-budget` | `arm-none-eabi-size`, from gcc-arm-embedded |
| `python3 -m arcane_host.city_window` | tkinter (`python3-tk` on Debian) |
| `tools/contact_sheet.py`, `tools/figures.py` | Pillow (`python3-pil` on Debian) |

The browser shell needs a clang with the `wasm32` target and `wasm-ld`, plus
Node for the parity harness. That is a heavier ask than the rest of the tree
needs, so `web-lib` and `web-parity` are deliberately outside `make test`:
nobody working on the firmware should have to install a WebAssembly toolchain
to run the firmware's own gates. Run `make web-parity` when changing anything
the browser build compiles, which is the shared simulation, the desktop
drawing layers, or `web/` itself.

On a distribution whose clang is wrapped for the host target -- NixOS, for one
-- use the unwrapped compiler, because the wrapper injects host linker flags
that `wasm-ld` rejects:

```bash
nix shell nixpkgs#llvmPackages.clang-unwrapped nixpkgs#lld nixpkgs#nodejs
```

Use `make format` to apply the repository baseline: Python 3.10 syntax, Ruff
imports and correctness rules, and 100-column Python/C formatting. Generated
artifacts, layout data, and golden hashes are excluded.

## The browser build's own gate

Determinism is a claim the browser makes out loud: the seed and the tick are
in the URL, so a link promises the recipient the same world, spell for spell.
`make web-parity` is what makes that a fact rather than a hope. It renders the
same matrix -- every layout, three seeds, 240 frames each -- twice, once
through the WebAssembly module and once through the native library the desktop
window loads, and fails unless every byte of all 4 320 frames agrees.

Both sides drive the self-playing world and the town's residents beside it, so
a divergence in the simulation shows up as well as one in the renderer: the
run's hashes stop matching partway through instead of at the first frame. Each
case also compares the world's tallies and a hash of the residents' handle, so
a divergence there shows even where no frame draws it. A second set of rows
sets the host semantics and every off-keyboard signal away from its default,
432 frames more, and a last check confirms that seeking to a moment, as a link
does, arrives at the same world as watching into it. The matrix lives in
`web/tools/parity_matrix.json` and is read by both sides, so the two cannot
drift apart. `make swift-parity` runs the same matrix a third time, through the
Swift package the Apple apps are built on, and diffs it against the same native
reference.

## Golden review

`firmware/sim_test/golden/visual_current.hashes` contains one exact framebuffer
hash per scene in the visual catalog. Build a reviewable sheet with:

```bash
firmware/sim_test/visual_runner --dump-pgm /tmp/frames
python3 tools/contact_sheet.py /tmp/frames sheet --only district_
```

 Do not regenerate it as a routine response to a failure. First build a
reviewable dump/contact sheet, inspect the changed scenes and protected regions,
and establish that the visual change is intentional. Update the golden only in
the same change that explains and tests the new presentation contract.

```bash
make -C firmware/sim_test visual-golden   # only after the sheet is reviewed
```

The figures in the documentation come from the same dump, so a reviewed visual
change is also a figure change:

```bash
python3 tools/figures.py /tmp/frames docs/images
```

Regenerate them in the change that moves the golden, and review the result as
part of the same inspection. A figure that disagrees with the catalog is a
stale figure, not a new contract.

Mechanics test names and PASS output are stable diagnostics. Add focused cases
to the appropriate suite: runtime/display/RGB, protocol/view,
incantation/compiler, combat/lifecycle, civic/presentation, or
rendering/geometry. Shared deterministic helpers belong in the test harness.

## Release budgets

Run both release commands after firmware changes. `release-budget` enforces the
88 KiB flash and 16,496-byte static-RAM ceilings, the current-baseline +512-byte
RAM allowance, the 96 KiB hard stop, and at least 8 KiB reserve. Investigate
growth before considering a budget change; do not raise a ceiling to make an
unrelated refactor pass.

Generated ELF, UF2, and map files are ignored working artifacts. The last
clean pinned-QMK build, `arm-none-eabi-gcc 15.2.rel1`:

| Image | Flash | Static RAM | Reserve below 96 KiB |
|---|---:|---:|---:|
| release | 85,356 B | 13,552 B | 12,948 B |
| diagnostic | 86,736 B | 13,680 B | 11,568 B |

Record the compiler alongside any figure you compare against: it moves these
numbers more than most changes do. The binding constraint is the 88 KiB flash
ceiling on the diagnostic image, not the hard-stop reserve.

Flashing hardware is a separate procedure; see [`flashing.md`](flashing.md).

`VIAL_QMK_REVISION` is the sole accepted Vial-QMK pin. `release-build` rejects
a checkout at any other revision and prints the deliberate development
override and pin-update options. The scheduled firmware workflow clones that
revision recursively, builds both images, enforces the same budgets, and keeps
ELF, UF2, map, hash, and budget files for 14 days. Those files record a build;
they are not a published release, and they do not show the firmware works on
a keyboard. That still takes the checks in
[`flashing.md`](flashing.md#checking-it-worked). Published releases come from
`fw-v*` tags through `.github/workflows/release.yml`, which renames the release
image to `corne_arcane.uf2`.

## Safe module extraction

When moving code across modules:

1. Identify the real production boundary and keep test-only helpers in test
   support.
2. Preserve public command names, D-Bus identities, protocol layouts, state
   field order, phase order, and timing constants.
3. Add new translation units explicitly to both native and QMK source lists.
4. Keep private cross-module calls in an internal header; avoid making helpers
   public merely to satisfy tests.
5. Run sanitizer mechanics tests and exact visual goldens before and after the
   extraction, then build the release and diagnostic images and
   compare resource use.
6. Commit formatting separately from semantic or structural edits.

## Comments and history

Code comments explain the current invariant, ownership rule, ordering
constraint, wire allocation, or reason a surprising implementation is needed.
They do not narrate when a feature landed, which planning stream owned it, or
what an earlier implementation looked like. That history is in `git log`;
keep documentation about the current system and compatibility requirements.
