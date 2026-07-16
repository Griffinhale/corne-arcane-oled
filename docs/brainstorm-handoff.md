# Corne Arcane brainstorming handoff

Copy everything below into a new chat.

---

I want you to be a collaborative product and technical design partner for the
next phase of a completed Corne Arcane OLED project. Help me brainstorm and
debate directions before writing any code. Ask a few high-leverage questions,
challenge weak assumptions, and keep recommendations grounded in the tiny
hardware and the ambient experience.

## Accepted baseline

The device is a Corne v3 RP2040 split keyboard with one 128x32 monochrome OLED
on each half, mounted in portrait and separated by the physical desk gap. M13 is
implemented and has passed automated, QMK, budget, and physical two-half checks.
Normal typing is never delayed, consumed, or rewritten by display logic.

The firmware runs one deterministic fixed-tick world. The USB master owns
authoritative shared state; the other half renders synchronized snapshots and
has a local pose-only fallback if the link goes stale. The host is optional
semantic enrichment, never required for a coherent keyboard.

M13 already includes:

- A privacy-preserving physical-position incantation compiler driven by timing,
  holds, layers, rolls, and release patterns—not keycodes, characters, words, or
  retained input history.
- Eight spell forms: projectile, singularity, roof fireball, beam, swarm,
  ground wave, chain/arc, and conjure summon/trap.
- Twelve HP, prepared casts, expanding consumable wards, healing, statuses,
  clashes, combinations, detonations, reflection/absorption, area pulses,
  spatial lanes, aftermath, KO/medic/replacement, and roster variation.
- Twin Cities presentation with Astral and Mechanical voices; Commons,
  Research, and Workshop floors; residents, couriers, rare events, occupation
  objects, focus transitions, and authoritative civic aftermath.
- A deliberate layer-key scry overlay, notification glyphs, application-focus
  semantics, privacy-redacted notification/media/network/timer/Git context, and
  sleep/reconnect/stale-link behavior.

## Possible future scope

These are ideas, not commitments:

1. Define the reserved `SPECIAL` floor/world and what selects it.
2. Add application worlds beyond Commons, Research, and Workshop without making
   every application a bespoke scene.
3. Add denser or multi-page scry views while keeping the gesture deliberate and
   the underlying world active.
4. Deepen existing cities with more Archive objects, occupations, ambient
   behaviors, couriers, rare events, residents, medic variants, and roster
   voices.
5. Add bounded spell forms, payloads, shallow triggers, interactions, aftermath
   arcs, or outcome grammars beyond M13's eight forms.
6. Add coarse browser activity such as scroll, tab, or page-event semantics,
   without transmitting content, titles, URLs, or browsing history.
7. Add shell adapters beyond the current Zsh/Git hook.
8. Add new privacy-bounded host semantic categories, application profiles, or
   desktop-platform adapters.

## Non-negotiable constraints unless we explicitly decide to reverse one

- Typing independence and a complete firmware-only fallback.
- One bounded, deterministic authoritative simulation; no general scripting,
  unbounded chains, unrestricted entity pools, or expensive pathfinding/needs
  simulation.
- Host messages are compact semantics only: no text, URLs, content, bitmaps, or
  streamed framebuffers.
- The scry gesture stays deliberate, normal layer rolls remain safe, and host
  events never wake sleeping OLEDs.
- Protected OLED regions, physical-gap readability, low visual restlessness,
  stale-link recovery, no-allocation gates, and exact deterministic/visual tests
  remain important.
- `griffin_arcane` keeps Vial; `griffin_arcane` uses custom Raw HID. They are
  separate because Vial and the daemon compete for QMK's Raw HID interface.
- The M13 split v10 packet is exactly 32 bytes, so there is no spare inherited
  split byte. New synchronized state requires derivation, repacking, reuse, or a
  protocol-version change.
- Raw HID is also fixed at 32 bytes.
- The Vial M13 release is 80,972 flash bytes: only 948 bytes below the 80 KiB
  target and 17,332 bytes below the 96 KiB hard stop. Future work needs an
  explicit flash budget and rollback plan.
- No permanent progression, save files, or long-lived host database under the
  current product direction.

## How I want to explore this

Start by asking me 3–5 high-leverage questions about the feeling, frequency,
and purpose of the next addition. Then help me compare at least three coherent
directions—for example a `SPECIAL` world, deeper Twin Cities, richer spells, or
richer scry/host semantics.

For each direction, discuss:

- The user-visible fantasy and why it improves an ambient keyboard rather than
  merely adding features.
- What belongs to authoritative simulation, disposable presentation, or host
  external context.
- OLED readability and restlessness risks.
- Privacy, protocol, flash/RAM, timing, testing, and rollback implications.
- The smallest physical spike that could validate the riskiest assumption.

End with a comparison, a tentative recommendation, and a deliberately bounded
next-milestone concept. Do not write implementation code or a detailed task
plan until we agree on a direction.

---
