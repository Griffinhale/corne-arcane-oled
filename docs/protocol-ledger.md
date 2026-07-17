# Protocol bit ledger

This ledger is the allocation authority for the two fixed 32-byte transports.
Raw HID remains version 2 and split snapshots remain version 10. Integers wider
than one byte are little-endian. Every reserved bit must be zero; receivers
reject malformed version, length, reserved-bit, enum, or CRC combinations.

## Raw HID v2 host report (32 bytes)

| Offset | Size | Field | Allocation |
|---:|---:|---|---|
| 0 | 1 | `magic0` | `0xCA` |
| 1 | 1 | `magic1` | `0x8E` |
| 2 | 1 | `version` | `2` |
| 3 | 1 | `type` | hello `1`, heartbeat `2`, notify `3` |
| 4 | 4 | `session` | daemon session nonce |
| 8 | 2 | `seq` | per-session sequence, wrapping |
| 10 | 1 | `payload_len` | exactly `8` |
| 11 | 20 | `payload` | first eight bytes below; bytes 8–19 zero |
| 31 | 1 | `crc` | CRC-8 over bytes 0–30 |

Payload bytes are scene, notification count, category, priority, age,
persistent, civic, and secondary. Civic bits are floor 0–1, mode 2–3,
host intensity 4–5, and reserved 6–7. Raw HID secondary bits 0–2 carry the
activity channel; bits 3–7 must be zero. The host never supplies sky phase.

## Split snapshot v10 (32 bytes)

| Offset | Size | Field | Allocation |
|---:|---:|---|---|
| 0 | 1 | `magic` | `0xA7` |
| 1 | 1 | `ver` | `10` |
| 2 | 1 | `session` | master boot nonce |
| 3 | 1 | `flags` | world-valid bit 0; display phase bits 1–2; bits 3–7 reserved |
| 4 | 2 | `seq` | incremented on every attempted send, including cadence skips |
| 6 | 19 | `view` | canonical duel/render projection |
| 25 | 1 | `external` | host online/scene/count/persistent summary |
| 26 | 1 | `alert` | category bits 0–2, priority 3–4, age 5–7 |
| 27 | 1 | `civic` | floor 0–1, mode 2–3, intensity 4–5; bits 6–7 reserved |
| 28 | 1 | `secondary` | activity 0–2, sky phase 3–4, bits 5–7 reserved |
| 29 | 1 | `shared_pres` | visitor or aftermath payload, selected by `revision.7` |
| 30 | 1 | `revision` | event or aftermath payload; bit 7 is the discriminator |
| 31 | 1 | `crc` | CRC-8 over bytes 0–30 |

Sky values are dawn `0`, day `1`, dusk `2`, and night `3`. A stale half runs
its local cycle; the next accepted master snapshot replaces the phase directly
without replay.

Within the 19-byte view, `outcome_overlay` uses bits 0–3 for the one-shot
`FX_*` kind, bit 4 for the scry-open overlay, bits 5–6 for scry scene, and keeps
bit 7 reserved. Scry is a presentation overlay and does not change civic wire
ownership.

## Shared presentation discriminator

When `revision.7 == 0`, `shared_pres` is courier kind 0–2, city 3, lifecycle
4–5, and density 6–7. `revision` is rare-event id 0–2, phase 3–4, target 5–6,
with bit 7 clear. Diplomatic targets encode left advantage `0`, right
advantage `1`, or balance `2`.

When `revision.7 == 1`, authoritative aftermath owns both bytes. `shared_pres`
contains left and right aftermath kinds in bits 0–2 and 3–5 plus world state
in 6–7. `revision` contains left and right phases in 0–1 and 2–3, reserved zero
bits 4–6, and the discriminator in 7. Aftermath suppresses ordinary courier and
rare-event interpretation.

Rendering precedence is sky underlay, floor, combat/champions, health, alert,
scry, recovery, and diagnostics. A source floor is shown during transition
phases 0–1; the target floor owns phases 2–3. Observatory suppresses ordinary
couriers, rare events, and energetic typing accents, while combat, alert,
health, scry, transitions, and authoritative aftermath remain visible.
