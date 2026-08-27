# Development workflow

## Toolchain and builds

Native tests require a C11 compiler with ASan/UBSan, Python 3.10 or newer,
Ruff, and clang-format 19-compatible configuration. Exact development-tool
versions are in `requirements-dev.txt`. Firmware builds use the configured
Vial-QMK checkout, `crkbd/rev1`, and `CONVERT_TO=rp2040_ce`.

```bash
make lint            # Ruff plus Python/C formatting checks
make test            # mechanics, 622 visual scenes, allocation scan, host tests
make hp-gate         # pinned 8-HP and 10-HP 30-minute workloads
make mechanics-hp-candidates # full mechanics suite under both geometries
make hygiene         # active-tree naming and historical-comment policy
make release-build   # release, diagnostic, and both unflashed HP candidates
make release-budget  # flash, static RAM, hard-stop, and reserve gates
git diff --check
```

Use `make format` to apply the repository baseline: Python 3.10 syntax, Ruff
imports and correctness rules, and 100-column Python/C formatting. Generated
artifacts, archived documents, layout data, and golden hashes are excluded.

## Golden review

`firmware/sim_test/golden/visual_current.hashes` contains 622 exact framebuffer
scenes. Build a reviewable sheet with:

```bash
firmware/sim_test/visual_runner --dump-pgm /tmp/frames
python3 tools/contact_sheet.py /tmp/frames sheet --only district_
```

 Do not regenerate it as a routine response to a failure. First build a
reviewable dump/contact sheet, inspect the changed scenes and protected regions,
and establish that the visual change is intentional. Update the golden only in
the same change that explains and tests the new presentation contract.

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
| 8-HP candidate | 85,356 B | 13,552 B | 12,948 B |
| 10-HP candidate | 85,364 B | 13,552 B | 12,940 B |

Record the compiler alongside any figure you compare against: it moves these
numbers more than most changes do. The binding constraint is the 88 KiB flash
ceiling on the diagnostic image, not the hard-stop reserve.

Flashing hardware is a separate procedure; see [`flashing.md`](flashing.md).

`VIAL_QMK_REVISION` is the sole accepted Vial-QMK pin. `release-build` rejects
a checkout at any other revision and prints the deliberate development
override and pin-update options. The scheduled firmware workflow clones that
revision recursively, builds both images, enforces the same budgets, and keeps
ELF, UF2, map, hash, and budget evidence for 14 days. Workflow artifacts are
build evidence only: they are never a GitHub release and never replace the
signed physical checklist.

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
   extraction, then build release, diagnostic, and HP A/B candidates and
   compare resource use.
6. Commit formatting separately from semantic or structural edits.

## Comments and history

Code comments explain the current invariant, ownership rule, ordering
constraint, wire allocation, or reason a surprising implementation is needed.
They do not narrate when a feature landed, which planning stream owned it, or
what an earlier implementation looked like. Preserve useful implementation
history under `docs/archive/`; keep active documentation about the current
system and compatibility requirements.
