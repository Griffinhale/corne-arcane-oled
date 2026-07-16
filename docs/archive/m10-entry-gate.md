# M10 entry gate for M11

Date: 2026-07-13

> Historical milestone record. The accepted M13 two-half run on 2026-07-15
> supersedes this gate. Unchecked items below describe what remained at M10;
> they are not current project blockers.

M11 does not retroactively declare M10 accepted. The evidence is split into
desktop and physical gates so hardware claims remain auditable.

## Desktop gate — passed

- Firmware/simulation suite: all deterministic replays, sanitizers,
  no-allocation checks, protocol tests, Archive tests, alert tests, and privacy
  boundaries passed before M11 visual changes.
- Host suite: 36/36 tests passed, including desktop notification replacement
  and dismissal, focused-source suppression, plaintext redaction, monitor
  denial isolation, Zsh async/threshold policy, HID absence/reconnect, fresh
  sessions, KWin restart, and heartbeat priority.
- M10 candidate baseline: 49,512 flash bytes and 9,676 bytes `.bss`.
- M10 candidate ELF SHA-256:
  `04f717c38ab343368f294cccfa944f4898f9479c4be1faf1db1326aac07323b6`
- M10 candidate UF2 SHA-256:
  `e3247e3efe0efb2f111c7bc9e6055c80dcd24e66a3d1cc516f4b081ab3d8b348`

## Physical gate — pending at M10 (historical)

M10 remains hardware-smoke-tested, not fully accepted, until all of these are
rerun on the assembled keyboard:

- Desktop notification source/replacement/dismissal and focused suppression.
- Real Zsh completion hook and cooldown behavior.
- Sustained two-half typing plus host-event flood; ordered, immediate key output.
- Host suspend/resume, Raw HID disconnect/reconnect, daemon restart.
- Notification-monitor denial with focus and typing fallback intact.
- Rollback firmware flash and recovery-keymap verification.

Partial physical evidence recorded during the M11 flash on 2026-07-13:

- Both halves still boot and type with the M11 firmware pair.
- Persistent notification state recovers across USB disconnect/reconnect.
- Both OLEDs power off after five minutes without a physical key press.

These results reduce recovery risk but do not replace the remaining full M10
desktop-adapter, Zsh, stress, suspend, monitor-denial, and rollback sequence.
