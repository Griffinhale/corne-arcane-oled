# Corne Arcane exploration backlog

M13 is the accepted baseline. Nothing here is required to complete M13; these
are possible directions for a future milestone and need product/design
discussion before implementation.

## Presentation worlds and information surfaces

- Define the reserved `SPECIAL` floor/world and decide what host or firmware
  state selects it. Current firmware deliberately ignores `SPECIAL` targets.
- Add application worlds beyond Commons, Research, and Workshop without turning
  every application into a bespoke scene.
- Add denser or multi-page scry views while keeping the gesture deliberate and
  the underlying world active.
- Expand Archive objects, city occupations, ambient behaviors, couriers, rare
  events, residents, medic variants, and roster voices within the OLED density
  and protected-region limits.

## Input-driven fiction and combat variety

- Explore additional bounded spell forms, payloads, shallow triggers,
  interactions, aftermath arcs, and outcome grammars beyond M13's eight forms.
- Consider richer browser activity such as coarse scroll, tab, or page-event
  semantics. Any adapter must remain optional, privacy-redacted, and semantic;
  firmware must never receive content, URLs, titles, or streamed frames.
- Preserve one authoritative master simulation, hard entity/chain caps,
  deterministic fixed ticks, and typing-path independence.

## Host integration

- Add shell completion adapters beyond the existing Zsh/Git hook.
- Consider new privacy-bounded host semantic categories and application
  profiles beyond Raw HID v2's current normalized summary.
- Explore broader desktop support behind adapters without coupling firmware to
  KWin, Plasma, D-Bus, or one operating system.

## Architecture constraints for any next milestone

- M13's split v10 packet is exactly 32 bytes. New authoritative or synchronized
  presentation state requires repacking, state reuse, derivation, or a new
  version; there is no inherited five-byte v8 reserve.
- Raw HID remains a fixed 32-byte semantic protocol and shares the Vial
  interface, so `griffin_arcane` and `griffin_arcane` remain separate unless
  that conflict is deliberately redesigned.
- The Vial M13 release is 80,972 flash bytes: 948 bytes below the 80 KiB target
  and 17,332 bytes below the 96 KiB hard stop. Future content needs an explicit
  flash budget and must preserve at least the accepted safety reserve or revise
  the budget intentionally.
- Static RAM growth, stack headroom, split/OLED timing, no-allocation gates,
  power policy, stale-link recovery, and exact visual/determinism tests remain
  release gates.
- No permanent progression, save files, long-lived host database, unrestricted
  entity pool/pathfinding/needs simulation, host-supplied text or bitmaps, or
  streamed framebuffers without an explicit product-level reversal of the
  current scope guards.

## Questions to settle before coding

1. Which direction adds the most ambient value: a `SPECIAL` world, deeper
   existing cities, richer spells, or richer scry/host semantics?
2. Should the next milestone spend protocol bytes, flash reserve, or both—and
   what rollback artifact and hard stop protect the accepted M13 baseline?
3. Which additions are authoritative mechanics versus disposable presentation
   or external context?
4. How will each idea remain readable on two 128x32 portrait OLEDs across the
   physical desk gap without increasing restlessness?
5. What is the smallest physical spike that can validate the riskiest unknown
   before broad implementation?
