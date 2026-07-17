# Corne Arcane brainstorming handoff

> Historical milestone record — superseded by the landed M15 build. This was a
> brainstorming handoff prepared before M15; the direction it seeded now lives
> in `../acceptance.md` (accepted M15 state) and `../backlog.md` (v12 seeds).

Copy everything below into a new chat.

---

I want you to be a collaborative product and technical design partner for the
next phase of a completed Corne Arcane OLED project. Help me brainstorm and
debate directions before writing any code. Ask a few high-leverage questions,
challenge weak assumptions, and keep recommendations grounded in the tiny
hardware and the ambient experience.

## Accepted baseline

The device is a Corne v3 RP2040 split keyboard with one 128x32 monochrome OLED
on each half, mounted in portrait and separated by the physical desk gap. Version 0.4 is
implemented and has passed automated, QMK, and budget checks; its inherited
world/combat behavior has passed the recorded physical two-half checks.
Normal typing is never delayed, consumed, or rewritten by display logic.

The firmware runs one deterministic fixed-tick world. The USB master owns
authoritative shared state; the other half renders synchronized snapshots and
has a local pose-only fallback if the link goes stale. The host is optional
semantic enrichment, never required for a coherent keyboard.

The current firmware already includes:

- A privacy-preserving physical-position incantation compiler driven by timing,
  holds, layers, rolls, and release patterns—not keycodes, characters, words, or
  retained input history.
- Eight spell forms: projectile, singularity, roof fireball, beam, swarm,
  ground wave, chain/arc, and conjure summon/trap.
- Twelve HP, prepared casts, expanding consumable wards, healing, statuses,
  clashes, combinations, detonations, reflection/absorption, area pulses,
  spatial lanes, aftermath, KO/medic/replacement, and roster variation.
- Twin Cities presentation with Astral and Mechanical voices; Commons,
  Research, Workshop, and Pomodoro Observatory floors; residents, couriers,
  rare events, occupation objects, focus transitions, and authoritative civic
  aftermath.
- A firmware-owned 30-minute sky, independent per-half typing ambience,
  event-only session diplomacy, and a world-owned RGB surface.
- A deliberate layer-key scry overlay, notification glyphs, application-focus
  semantics, privacy-redacted notification/media/network/timer/Git context, and
  sleep/reconnect/stale-link behavior.

## Possible future scope

These are ideas, not commitments:

1. Deepen the Observatory only after its pending physical acceptance.
2. Add application worlds beyond the current floors without making
   every application a bespoke scene.
3. Add denser or multi-page scry views while keeping the gesture deliberate and
   the underlying world active.
4. Deepen existing cities with more Archive objects, occupations, ambient
   behaviors, couriers, rare events, residents, medic variants, and roster
   voices.
5. Add bounded spell forms, payloads, shallow triggers, interactions, aftermath
   arcs, or outcome grammars beyond the current eight forms.
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
- `griffin_arcane` combines secure Vial and custom Raw HID. The safe launcher
  gives Vial exclusive endpoint ownership by pausing the daemon.
- The split v10 packet is exactly 32 bytes, so there is no spare inherited
  split byte. New synchronized state requires derivation, repacking, reuse, or a
  protocol-version change.
- Raw HID is also fixed at 32 bytes.
- The unified release is 72,164 flash bytes with 13,480 bytes static RAM; the
  diagnostic image is 73,288 and 13,608 bytes. Future work needs an explicit
  flash budget and recovery plan.
- No permanent progression, save files, or long-lived host database under the
  current product direction.

## How I want to explore this

Start by asking me 3–5 high-leverage questions about the feeling, frequency,
and purpose of the next addition. Then help me compare at least three coherent
directions—for example deeper Twin Cities/Observatory, richer spells, or richer
scry/host semantics.

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
