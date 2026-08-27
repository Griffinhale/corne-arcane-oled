# Protocol bit ledger

This ledger is the allocation authority for the two fixed 32-byte production
transports. Raw HID is version 3 and split snapshots are version 12. Integers
wider than one byte are little-endian. Receivers reject malformed identity,
version, length, enum, canonical-zero, range, or CRC combinations; mixed
versions never downgrade.

## Raw HID v3 host report (32 bytes)

| Offset | Size | Field | Allocation |
|---:|---:|---|---|
| 0 | 1 | `magic0` | `0xCA` |
| 1 | 1 | `magic1` | `0x8E` |
| 2 | 1 | `version` | `3` |
| 3 | 1 | `type` | hello `1`, heartbeat `2`, notify `3` |
| 4 | 4 | `session` | daemon session nonce |
| 8 | 2 | `seq` | per-session sequence, wrapping |
| 10 | 1 | `payload_len` | exactly `8` |
| 11 | 20 | `payload` | first eight bytes below; bytes 8–19 zero |
| 31 | 1 | `crc` | CRC-8 over bytes 0–30 |

Payload bytes are scene, notification count, category, priority, age,
persistent, civic, and secondary. Scene is duel `0`, archive `1`, focus `2`, or
revel `3`; receivers reject `4` and above. Raw HID spends a whole byte on it,
but the split snapshot does not, so `3` is the last value the enum can hold --
see the external byte below. The firmware never renders a scene by name: it
pairs the scene with the civic floor to derive one of eight districts. Civic bits are floor 0–1, mode 2–3,
intensity 4–5, and reserved-zero 6–7. Secondary bits 0–2 are none `0`, media
`1`, transfer `2`, system `3`, calendar `4`, browser scroll `5`, tab selection
`6`, or page event `7`; bits 3–7 are zero. Firmware v12 rejects Raw HID v2.

`ReportBrowserActivity(yy)` accepts only secondary values 5–7 and intensity
0–3. Host adapters coalesce reports to at most 4 Hz and clear browser activity
after 1.5 seconds. The browser extension/native host carries exactly `kind`
and `intensity`, never page identity or content.

## Diagnostic v2 (diagnostic images only)

Diagnostics retain their separate version-2 protocol; this is not production
Raw HID v2 compatibility. Requests and responses use three 32-byte pages:
magic `CA 8E`, version `2`, type `0x70` request or `0x71` response, page, page
count, 16-bit nonce, 23-byte payload, and CRC-8. Requests zero page count and
payload. Responses set page count to three and echo page and nonce. A v1 reply,
a mixed page count, or a nonzero reserved byte is rejected.

Pages 0 and 1 carry counters/timing. Page 2 payload bytes 0–1 are master
minimum free stack, 2–3 peer minimum free stack, and 4–22 reserved zero. The
reverse split diagnostic reply remains 18 bytes. Diagnostics are absent from
release images.

## Split snapshot v12 (32 bytes)

Version and magic share byte 0 as `signature_version = 0xAC`: signature nibble
`0xA`, version nibble `12`. A v11 receiver sees invalid magic and enters its
existing stale-link presentation.

| Offset | Size | Field | Allocation |
|---:|---:|---|---|
| 0 | 1 | `signature_version` | exactly `0xAC` |
| 1 | 1 | `session` | master boot nonce and variance seed |
| 2 | 1 | `flags` | world-valid 0; display phase 1–2; residue zone 2 element 3–4/intensity 5–6; zone 3 intensity low bit 7 |
| 3 | 1 | `seq` | wrapping byte |
| 4 | 1 | `residue` | zone 0 element/intensity nibbles, then zone 1 |
| 5 | 18 | `view` | canonical duel/render projection |
| 23 | 1 | `field[0]` | kind 0–2, zone 3–4, age quarter 5–6, owner 7 |
| 24 | 1 | `field[1]` | same; zero is the only inactive encoding |
| 25 | 1 | `external` | online 0; scene 1-2; notification count 3-6; persistent 7 |
| 26 | 1 | `alert` | category 0–2, priority 3–4, age 5–7 |
| 27 | 1 | `civic` | floor 0–1, mode 2–3, intensity 4–5; residue zone 3 element 6–7 |
| 28 | 1 | `secondary` | activity 0–2, sky phase 3–4, sub-phase 5–6; zone 3 intensity high bit 7 |
| 29 | 1 | `shared_pres` | visitor or aftermath payload, selected by `revision.7` |
| 30 | 1 | `revision` | event or aftermath phase/flavor; bit 7 discriminator |
| 31 | 1 | `crc` | CRC-8 over bytes 0–30 |

The 18-byte view keeps the six wizard bytes, seven shared spell bytes,
`fx_stance`, `outcome_overlay`, two phase bytes, and `status_visual`. Each
active spell occupies a 20-bit descriptor plus 8-bit progress. The descriptor
retains form, element, payload, trajectory, magnitude, status, tempo, trend,
and valid. Interaction reconstructs as ABSORB for singularity, PHASE for other
void, and SOLID otherwise; master-local COMBINE is intentionally rendered as
SOLID. Variance reconstructs as `CRC8(session, side, compressed) & 3`. The
exhaustive compile-domain test pins all slave-observable fields and both
reconstructions. Inactive descriptors require zero progress.

Field kinds are none `0`, trap `1`, singularity `2`, steam cloud `3`, rune `4`,
familiar `5`, wall `6`, and vortex `7`. Zones are doorstep-L, mid-L, mid-R,
and doorstep-R. Age is the elapsed lifetime quarter. The global slots are
authoritative; only this display projection crosses the split.

Scene occupies exactly two bits of `external`, which is why the host enum is
full at four values. Floor occupies exactly two bits of `civic`. The renderer
derives its districts from that pair in a fixed rule order, checking every exact
(floor, scene) combination before any floor-only fallback: Workshop with archive
is the Undercroft and Workshop otherwise is the Workshop; Special with focus is
the Observatory; Research with duel is the Scriptorium; Commons with archive is
the Studio and Commons with revel is the Arena; the remaining pairs read as
Research, Observatory, or Commons by floor alone. No district, and no per-application
identity, occupies wire state of its own.

Residue uses the same four zones. Intensity zero requires element zero. Zone 3
straddles `flags.7`, `secondary.7`, and `civic.6–7`; accessors in
`duel_proto.h` are the sanctioned interface. The civic setter preserves these
borrowed bits.

## Shared presentation discriminator

With `revision.7 == 0`, `shared_pres` is courier kind 0–2, city 3, lifecycle
4–5, density 6–7. `revision` is rare-event id 0–2, phase 3–4, and target 5–6.

With `revision.7 == 1`, aftermath owns both bytes. `shared_pres` carries left
and right kinds in bits 0–2 and 3–5 plus world state in 6–7. `revision` carries
left/right phase in 0–1 and 2–3, aftermath flavor in 4–6, and the discriminator
in 7. Flavors are base `0`, rune `1`, familiar `2`, wall `3`, vortex `4`, echo
`5`, and bloom `6`; `7` is rejected.

Rendering precedence keeps stale/diagnostic indicators ahead of ordinary
decoration. Each OLED owns a narrow, black-interior almanac scroll: seven local
presentation extents unroll from the centre, labelled values advance upward
while the deliberate chord remains held, and release freezes then rerolls the
reading. These frames add no wire state and never pause combat or wake sleeping
hardware. Observatory suppresses crowds and disposable energetic ambience, not
authoritative combat or warnings.
