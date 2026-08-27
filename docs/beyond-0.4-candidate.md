# Beyond-0.4 candidate record

Status: implemented and automated-testable, but not accepted. The 0.4 physical
checklist was accepted on 2026-07-22, so controlled physical evaluation is now
authorized. Preserve the accepted 0.4 hashes/artifacts in `acceptance.md` and
the `griffin` recovery image throughout evaluation.

## Implemented candidate

- Split snapshot v12 uses `0xAC`, an 18-byte view, two compressed spell
  projections, exactly two field projections, strict validation, deterministic
  reconstructed interaction/variance, and aftermath flavor bits.
- The authoritative world has two global field slots: trap, singularity, steam,
  rune, familiar, wall, and vortex. Field collision precedes spell collision;
  slot exhaustion resolves immediately. Rune, Familiar, Wall, Vortex, Echo,
  and Bloom are physical-input-reachable derived signatures.
- Eight broad districts—Commons, Research, Workshop, Observatory, Scriptorium,
  Studio, Arena, and Undercroft—share two city voices. Each is derived from a
  (civic floor, host scene) pair; none has wire state of its own. DND preserves
  the focused district and applies QUIET. Only active Pomodoro selects
  Observatory, whose four stages change at duration quarters (default 1,500
  seconds). Background media no longer overrides a recognised application, so a
  focused window keeps its own district while something is playing.
- Scry is a three-page diegetic World Almanac (City, Duel, Host). Each OLED
  receives its own outlined scroll with explicit labels and local values. It
  unrolls from the centre, moves its reading upward while held, and freezes
  before rerolling on release; a reduced-motion build can snap directly to the
  same readable state. The selected page is authoritative and host scene never
  replaces it. Crowds derive at
  most two bystanders plus the resident, last no longer than default aftermath,
  and are suppressed in QUIET/Observatory.
- Raw HID v3 adds scroll `5`, tab `6`, and page `7`. Browser reports are
  validated, coalesced to 4 Hz, expire after 1.5 seconds, and obey
  system > transfer > calendar > page > tab > scroll > media precedence. The
  three browser events use distinct, quiet-suppressed Research instruments.
- Bash/Fish/Zsh hooks, opt-in GNOME assets, and opt-in Firefox/native-messaging
  assets are packaged. Their interfaces exclude command text, paths,
  repository identity, window titles, URLs, content, history, forms,
  referrers, and typed text.

## Automated evidence

- The exact visual catalog has 622 hash-pinned scenes. Focused contact sheets
  cover all district/mode/intensity combinations, four Observatory stages,
  three almanac pages, the unroll/held/reroll sequence, maximum crowd moments,
  all seven fields at four zones, and scroll/tab/page Research instruments at
  every intensity. Simulator-scale review passed; physical OLED desk-distance
  review is still pending.
- The descriptor compiler domain is exhaustively enumerated. Mechanics tests
  cover field ordering, caps, lifetimes, ownership, fallback, reconnect,
  malformed packets, signatures, crowds, districts, scry, sleep non-wake, and
  both HP geometries.
- Three pinned 30-minute intermittent-work workloads pass for both candidates
  with no KO interval below 90 seconds:

| Candidate | Workload medians | Aggregate median |
|---|---|---:|
| 8 HP, 2×4 shaft | 183 s, 242 s, 282 s | 237 s |
| 10 HP, 2×5 shaft | 196 s, 240 s, 296 s | 240 s |

`make hp-gate` records KO timestamps and verifies exact world/workload hashes.
`make mechanics-hp-candidates` runs the complete sanitizer mechanics suite
under both constants. Neither automated result selects the final HP constant;
the two representative physical sessions remain required.

## Resource ceilings

Release, diagnostic, and both HP candidate images must each stay at or below
88 KiB flash, 16,496 bytes static RAM, the current image baseline plus 512
bytes RAM, and the 96 KiB hard stop. Every image must retain at least 8 KiB
below that hard stop. `make release-build` creates both unflashed HP candidates;
`make release-budget` checks all four images. Record exact measurements and
hashes here only after a clean build.

Clean pinned-QMK build on 2026-08-27, `arm-none-eabi-gcc 15.2.rel1`:

| Artifact | Flash | Static RAM | Reserve below 96 KiB | SHA-256 (UF2) |
|---|---:|---:|---:|---|
| release (8-HP baseline) | 85,356 B | 13,552 B | 12,948 B | `c57adebbb8bf053082eb062bbef6d044ec781042b2e32b1823e5f12b85bbbc40` |
| diagnostic | 86,736 B | 13,680 B | 11,568 B | `2a13d894d233905e29c0cbb43154deec6341f7e7fb8f4a90e2cd774f436c07bb` |
| 8-HP candidate | 85,356 B | 13,552 B | 12,948 B | `e74188cc6f595981d1719e93285e27bf4af502bf4129d2732fc0320d90b41b2c` |
| 10-HP candidate | 85,364 B | 13,552 B | 12,940 B | `e945374d2beb88be1328286bee6b471dba56d1264873022b752846b81e00beb9` |

The compiler is recorded because it moves these numbers: the same tree built
before the Arena and Undercroft landed measured 84,472 B release flash under
this compiler, against 85,040 B in the previous row set. The districts
themselves cost 884 B of flash and no static RAM. Every image keeps at least
3,376 B below the 88 KiB ceiling and at least 11,568 B below the 96 KiB hard
stop.

The 622-scene golden catalog hash is
`da8a70a9e082d9628f07ee4faf5a2a3272ce921e55b282c1b6d3fa0f8c8dfb1c`.
These are unflashed candidate hashes and do not replace the 0.4 table.

## Physical gate (all pending)

- [x] Accept or explicitly supersede `physical-checklist.md`; archive its
  evidence without changing the recorded 0.4/recovery artifacts.
- [ ] Review the eight districts, four Observatory stages, all almanac pages,
  maximum crowds, and seven fields/four zones on both OLEDs at desk distance.
- [ ] Flash identical candidate artifacts to both unpowered, separated halves;
  repeat split timing/stack diagnostics, Vial handoff, sleep/non-wake, stale
  recovery, and the complete sky/Observatory cycle.
- [ ] Run one representative 30-minute physical session on each HP candidate,
  recording only KO timestamps. Select a candidate whose automated and
  physical medians are 3–5 minutes with no sub-90-second churn. If both miss,
  select the closer median; an exact tie selects 10 HP.
- [ ] Commit only the selected constant and its final visual goldens, then
  prove rollback by booting the preserved 0.4 image on both halves and
  restoring the selected release.
