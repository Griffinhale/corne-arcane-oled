# M11 candidate and acceptance record

> Historical milestone record. The accepted M13 two-half run on 2026-07-15
> supersedes this candidate gate. Unchecked items below describe what remained
> at M11; they are not current project blockers.

M11 was implemented, built, and flashed on both physical halves as a release
candidate. At that milestone it was not finally accepted because the remaining
gallery, stress, timing, suspend, and rollback checks below had not passed.

M11.1 subsequently hardens this candidate without changing protocols or
mechanics. Its measured release record and separate pending physical gate are
in `docs/m11.1-acceptance.md`; the measurements below remain the reproduced M11
baseline rather than being overwritten by M11.1.

## Deterministic artifacts

- Canonical framebuffer hashes: `firmware/sim_test/golden/visual.hashes`.
- Generate the complete paired gallery: `make gallery`.
- Run exact hashes and structural invariants: `make visual-test`.
- Run firmware/RAM/timing report: `make budget`.
- Raw HID remains v2/32 bytes; split snapshot remains v7/31 bytes;
  `sim_world_t` remains 56 bytes and mechanically unchanged.

## Candidate measurements

Fill from the final release build; do not copy diagnostic-build measurements.

| Item | Gate | Candidate |
| --- | ---: | ---: |
| Release flash | <= 65,536 bytes | 50,120 bytes |
| `.bss` growth from M10 9,676 | <= 2,048 bytes | +528 bytes (10,204 total) |
| RP2040 housekeeping peak | < 2 ms | pending physical diagnostics |
| RP2040 composition + blit peak | < 5 ms | pending physical diagnostics |
| Queue overflow / missed ticks / starvation / stale split under stress | 0 | pending physical stress |
| Release ELF / UF2 SHA-256 | recorded | candidate recorded below |

The opt-in `ARCANE_DIAGNOSTICS=yes` build exposes `duel_diag` for debugger/map
inspection: overflow, catch-up ticks, missed-tick resyncs, stale split edges,
split/host errors, and peak microsecond durations. The release build excludes
these counters and the diagnostic HUD.

## Display policy acceptance

- Active contrast is 128.
- After 60 seconds without a physical key press, contrast fades to 32 and
  ambient composition cadence drops from 50 ms to 250 ms.
- At five minutes both OLEDs receive a committed blank framebuffer and power
  off; simulation, host expiry/recovery, and split snapshots continue.
- A key press on either half wakes locally and synchronizes through reserved
  v7 flag bits; the peer must show the current snapshot within 100 ms.
- Host focus, notification, heartbeat, and lifecycle changes do not call the
  wake path.

## Physical evidence to date (2026-07-13)

- [x] The same M11 release candidate was flashed onto both halves.
- [x] Both halves boot and type normally with synchronized OLED presentation.
- [x] Host notifications continue to operate after the firmware update.
- [x] Persistent notification state recovers across USB disconnect/reconnect.
- [x] Both OLEDs reach full power-off after the five-minute physical-key idle
  interval. The preceding gradual contrast change is subtle on these panels.
- [ ] Either-half wake latency, host-events-never-wake behavior, and exact
  RP2040 diagnostic timing still require deliberate measurement.

## Remaining physical checklist

- Review `artifacts/m11-gallery/index.html`, then review all scenes at actual
  OLED size and across the real desk gap.
- Exercise every element/tier, impact/deflect/fizzle/VOID pierce, lifecycle
  phase, roster mark, Archive activity state, alert category/priority/age/
  persistence form, scry state, stale-link mark, and diagnostics overlay.
- Verify either-half wake under 100 ms, host events never wake a sleeping
  display, long idle continuity, suspend/resume, daemon restart, and split-link
  recovery. Five-minute blank/power-off and HID reconnect persistence have
  already passed.
- Flood host events while typing rapidly on both halves; require ordered,
  immediate key output, zero overflow/missed ticks/starvation/staleness, and
  synchronized displays.
- Re-run the complete M10 physical gate, then flash the preserved rollback
  firmware and verify typing/recovery before restoring the accepted M11 pair.

## Acceptance sign-off

- Gallery reviewer/date: pending
- Physical reviewer/date: pending
- Release commit: pending
- Candidate release ELF SHA-256:
  `1b3be883b3c13d83527d847e8183b4180f05130467fd42de43220471f436b6f9`
- Candidate release UF2 SHA-256:
  `99f4e33f41558fa3bb66bcacb58686ec84a5d0d7659cab0e60636cec115f1211`
- Rollback UF2 SHA-256:
  `99500f54d7b76de020158a755c7a60a5a48506a83dc949fc2fc22364293ce247`
  (`crkbd_rev1_griffin_anim_m75_verified_rp2040_ce.uf2`, hardware-accepted M7.5)
