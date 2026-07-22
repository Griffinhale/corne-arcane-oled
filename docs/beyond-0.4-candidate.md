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
- Six broad districts—Commons, Research, Workshop, Observatory, Scriptorium,
  and Studio—share two city voices. DND preserves the focused district and
  applies QUIET. Only active Pomodoro selects Observatory, whose four stages
  change at duration quarters (default 1,500 seconds).
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

- The exact visual catalog has 548 hash-pinned scenes. Focused contact sheets
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

Clean pinned-QMK build on 2026-07-22:

| Artifact | Flash | Static RAM | Reserve below 96 KiB | SHA-256 (UF2) |
|---|---:|---:|---:|---|
| release (8-HP baseline) | 85,040 B | 13,560 B | 13,264 B | `601bf1d87b6be87ca948071d6d8d7a13906ba4e79efa5a9074745810659a468a` |
| diagnostic | 86,460 B | 13,688 B | 11,844 B | `7b1cf15ce37f68e0165e2bac4f31bddb11354fc4ad64d542cb60e12897b60c40` |
| 8-HP candidate | 85,040 B | 13,560 B | 13,264 B | `d945648ca3338104fe40269350e3a685f681782b8d24eee2ac36f1d97db60a30` |
| 10-HP candidate | 85,056 B | 13,560 B | 13,248 B | `25793a9d50ae79eabce7bbb0c7378530e9ce59d339bd1b8523e802e6e4a3c11e` |

The 548-scene golden catalog hash is
`46850bc8da41c0d51907a0282521161a6260caa31d8b3389158c9a4dd235f571`.
These are unflashed candidate hashes and do not replace the 0.4 table.

## Physical gate (all pending)

- [x] Accept or explicitly supersede `physical-checklist.md`; archive its
  evidence without changing the recorded 0.4/recovery artifacts.
- [ ] Review the six districts, four Observatory stages, all almanac pages,
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
