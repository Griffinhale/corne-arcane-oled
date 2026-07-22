# Corne Arcane 0.4 diagnostic observation evidence

This directory preserves the diagnostic runs performed on both physical halves
on 2026-07-22. The passing run became evidence for the subsequently completed
[`0.4 physical acceptance`](../2026-07-22-0.4-physical-acceptance.md).

## Investigation sequence

1. [`attempt1-pre-fix-failed.json`](attempt1-pre-fix-failed.json) exposed a
   master housekeeping peak above 2,000 us and one split-transaction failure.
2. [`attempt2-pre-fix-failed.json`](attempt2-pre-fix-failed.json) reproduced the
   housekeeping result and recorded six split-transaction failures.
3. Investigation showed that the master housekeeping timer included the
   separately measured synchronous split wait, making the 2,000 us local-work
   gate impossible when the split transaction itself took more than 3,000 us.
   Commit `76ea23d` excludes that wait from the local housekeeping metric while
   retaining the separate split peak and strict failure counter.
4. After rebuilding and reflashing both halves, the canonical five-minute run
   [`physical-0.4-diagnostics.json`](physical-0.4-diagnostics.json) exited `0`
   with `passed: true`.

## Passing measurements

| Measurement | Master | Peer |
|---|---:|---:|
| Peak local housekeeping | 1,185 us | 968 us |
| Peak render + blit | 1,559 us | 2,276 us |
| Minimum free stack | 1,344 B | 1,304 B |
| Snapshot age after observation | — | 250 ms |
| Split success delta | +2,112 | — |
| Split failure delta | 0 | — |
| Queue overflow / missed resync / stale-link delta | 0 / 0 / 0 | 0 / 0 / 0 |

The run used adaptive 250 ms repair cadence. Protocol, malformed-host, and
stale-host counter deltas were also zero. The release candidate was flashed to
both halves afterward, and the user service was reported active.

## Candidate artifact hashes

| Artifact | SHA-256 |
|---|---|
| `griffin_arcane-release.uf2` | `4d5b2ffa6178e6ce14c6525cf29aa71f0ba84b8d559f5bb30f00c3038212552d` |
| `griffin_arcane-release.elf` | `93b76daf6b57cfe4f35b5457d3788a5c27a4a431ee269aaf556863997ac6f1c6` |
| `griffin_arcane-diagnostic.uf2` | `299eab9c282e3b4c378d1ba696adedf5b51f032cdf81c7f8a36d4f5c92fc7e30` |
| `griffin_arcane-diagnostic.elf` | `2d956d2c082644a9e8d9580a25f01be479e89f585005953affa121e6a84b0e1d` |
