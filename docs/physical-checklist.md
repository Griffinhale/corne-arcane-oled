# Corne Arcane 0.4 physical acceptance checklist

Status: pending for the current unified release artifacts. Prior reported
evidence and unchecked historical records are preserved verbatim in the
[dated acceptance archive](archive/2026-07-17-m14-m15-physical-checklist.md);
they do not mark the checks below complete.

Use `artifacts/release/griffin_arcane-diagnostic.uf2` on both halves for
measurement, then repeat the final smoke test with
`griffin_arcane-release.uf2`. Never connect or disconnect TRRS while either
half is USB-powered.

## Safety and flashing

- [ ] Export the current four-layer Vial mapping and preserve the accepted
  recovery image before changing either half.
- [ ] Disconnect USB, disconnect TRRS, flash the diagnostic artifact to the
  left controller, remove USB, and repeat with the identical artifact on the
  right controller.
- [ ] Reconnect TRRS only while unpowered, power the normal USB half, and
  confirm both displays leave stale-link mode and remain synchronized.
- [ ] Exercise every physical key on all four layers, thumb holds, rolls,
  chords, alternation, and long holds. Confirm every host key event arrives
  once and spell logic never consumes or rewrites typing.

## Vial handoff and persistence

- [ ] Confirm secure Vial unlock requires the configured physical combo.
- [ ] With `corne-arcane-host.service` active, launch `corne-arcane-vial`.
  Confirm the daemon stops before Vial opens, sends no competing traffic, and
  restarts after every normal, crash, and signal exit.
- [ ] Repeat with the daemon inactive and confirm the launcher leaves it
  inactive afterward.
- [ ] While Vial is open, confirm typing and the complete offline OLED/RGB
  world continue while only host semantic enrichment pauses.
- [ ] Edit keys on all four layers, close Vial, confirm a fresh daemon session,
  power-cycle the keyboard, and confirm all edits persist.
- [ ] Remap a layer/scry key and confirm the QMK gesture can become unavailable
  without changing physical-position incantation recognition. Reset the
  dynamic keymap and confirm the compiled default returns.

## Visual and mechanical verification

- [ ] Verify the four floors, Astral/Mechanical tower silhouettes, diegetic HP
  windows at 8/6/3/1/0, alerts, scry, transitions, aftermath, couriers, rare
  events, and gap cues remain legible at desk distance.
- [ ] Exercise every RGB priority on the correct half: city baseline,
  Observatory, prepared elements, ward shatter, impact, stale link, DIM, and
  SLEEP. Confirm remapped RGB keycodes cannot override world ownership.
- [ ] Observe the full 30-minute sky cycle and 16-step celestial arc at the
  2:30, 22:30, 25:00, and 30:00 boundaries without added animation churn.
- [ ] Type at different tempos on each half. Confirm ambience stays local,
  settles on launch/cancellation, and remains restrained in QUIET/Observatory.
- [ ] Verify residue marks build during ordinary dueling and decay on the
  approximately 45-second-per-step clock.
- [ ] Verify STUDY, MEDITATE, FORTIFY, PACE, and TAUNT presentation and the
  temperament/doctrine differences between replacement wizards.
- [ ] Time damaged-wizard regeneration: no HP returns before 20 seconds, one
  HP returns at the boundary, and intervening damage restarts the timer.
- [ ] Exercise normal typing-day combat and judge the current 8 HP KO cadence
  without changing tuning as part of this acceptance run.
- [ ] Let both displays sleep; confirm focus, Pomodoro, sky, world, and timer
  changes do not wake OLED or RGB, then wake both with a physical key.
- [ ] Interrupt split connectivity only while unpowered. Confirm stale local
  sky behavior and convergence to current master state without replay.

## Diagnostics and rollback

- [ ] Start the five-minute exclusive observation below, then continuously mix
  typing on both halves with maximal casts until it completes. Retain the JSON
  and its exit status; exit `0` is required (`1` is an operational failure and
  `2` is a failed acceptance gate).

  ```bash
  corne-arcane-diagnostics --observe 300 --json > physical-0.4-diagnostics.json
  ```

- [ ] Confirm the JSON contains exactly `before`, `after`, `deltas`, `checks`,
  and `passed`; `passed` must be true. Confirm peer validity, snapshot age at
  most 1,000 ms, increasing split successes, and nonzero master and peer stack
  minima.
- [ ] Confirm master and peer local housekeeping peaks are below 2,000 us and
  render peaks are below 5,000 us. The master housekeeping metric excludes the
  synchronous split wait, which is reported separately as `peak_split_tx_us`.
- [ ] Confirm no growth in master queue overflow, missed resyncs, stale-link
  events, split protocol errors, malformed/stale host reports, or split
  failures, and no growth in the corresponding peer counters. Record catch-up
  growth for context; it does not fail acceptance.
- [ ] Flash `griffin_arcane-release.uf2` to both halves using the same safety
  sequence. Repeat typing, Vial handoff/persistence, maximal aftermath,
  sleep/non-wake, and reconnect smoke tests.
- [ ] Verify the artifact hashes in `acceptance.md`, boot the preserved
  recovery image on both halves, then restore the accepted release.
- [ ] Only after every item passes, copy this signed checklist and the unchanged
  diagnostic JSON under a dated `docs/archive/` record, mark this active
  checklist complete, and update `acceptance.md` with the exact hashes,
  resources, measurements, sign-off date, tester, and archive links. If any
  item fails, leave acceptance pending and keep the existing accepted hashes.

| Measurement | Master | Peer |
|---|---:|---:|
| Peak housekeeping (us) |  |  |
| Peak render + blit (us) |  |  |
| Minimum free stack (bytes) |  |  |
| Queue overflow delta |  |  |
| Missed resync delta |  |  |
| Stale-link delta |  |  |

Candidate release UF2 SHA-256: ________________________________________________

Candidate diagnostic UF2 SHA-256: _____________________________________________

Diagnostic JSON archive path: _________________________________________________

Sign-off date: __________  Tester: __________  Result: __________
