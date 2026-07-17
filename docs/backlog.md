# Corne Arcane exploration backlog

Version 0.4 is the accepted software baseline. Nothing here is required to
complete it; these are possible directions for a future milestone and need
product/design discussion before implementation.

## Presentation worlds and information surfaces

- Deepen the Pomodoro-selected Observatory only after its pending desk-distance
  and full-cycle physical acceptance establishes that it remains calm/readable.
- Add application worlds beyond Commons, Research, and Workshop without turning
  every application into a bespoke scene.
- Add denser or multi-page scry views while keeping the gesture deliberate and
  the underlying world active.
- Expand Archive objects, city occupations, ambient behaviors, couriers, rare
  events, residents, medic variants, and roster voices within the OLED density
  and protected-region limits.

## Input-driven fiction and combat variety

- Explore additional bounded spell forms, payloads, shallow triggers,
  interactions, aftermath arcs, and outcome grammars beyond the current eight forms.
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

- The split v10 packet is exactly 32 bytes. New authoritative or synchronized
  presentation state requires repacking, state reuse, derivation, or a new
  version; there is no inherited five-byte v8 reserve.
- Raw HID remains a fixed 32-byte semantic protocol and shares the Vial
  interface. The safe launcher hands that one endpoint between the daemon and
  Vial; a future integration must preserve exclusive ownership.
- The unified release is 72,164 flash bytes and 13,480 bytes of static RAM;
  the diagnostic image is 73,288 and 13,608 bytes respectively. Future content
  needs an explicit flash budget and must preserve at least the accepted safety
  reserve or revise the budget intentionally.
- Static RAM growth, stack headroom, split/OLED timing, no-allocation gates,
  power policy, stale-link recovery, and exact visual/determinism tests remain
  release gates.
- No permanent progression, save files, long-lived host database, unrestricted
  entity pool/pathfinding/needs simulation, host-supplied text or bitmaps, or
  streamed framebuffers without an explicit product-level reversal of the
  current scope guards.

## v12 seeds (recorded at the M15 close-out, 2026-07-17)

Concrete inputs for the next protocol version, carried over from M15:

- **Field objects (Track C)**: ejected at the 2026-07-17 C gate on flash
  budget (~1.4 KiB projected against a milestone sitting 464 B under the
  margin line without it). Design of record in the M15 plan §7: a 2-entry
  field array (kind/zone/age/owner, 1 byte each on the wire), slot transfer
  when a trap arms or a singularity matures so the caster's in-flight slot
  frees, and an additive collision-ladder check before the spell-vs-spell
  ladder. Needs 2 wire bytes — the impetus for v12.
- **Descriptor wire compression**: drop the 4 interaction bits by
  substituting SOLID for COMBINE on the slave — the C-gate spike proved the
  slave-observable projection exact over the full compile domain, and the
  render-parity guard test now in the mechanics suite
  (`incantation_render_combine_solid_parity_all_elements_forms`) fails the
  moment a slave-side COMBINE visual appears. `variance`-by-session-seed
  frees 4 more bits (presentation jitter only; goldens re-baseline then).
- **HP tuning**: the desk-side 8-vs-10 HP verdict (M15 plan §10 Q4, judged
  during the M15 physical checklist) is the first v12 tuning input.

## Questions to settle before coding

1. Which direction adds the most ambient value after Observatory acceptance:
   deeper existing cities, richer spells, or richer scry/host semantics?
2. Should the next milestone spend protocol bytes, flash reserve, or both—and
   what recovery artifact and hard stop protect the accepted 0.4 baseline?
3. Which additions are authoritative mechanics versus disposable presentation
   or external context?
4. How will each idea remain readable on two 128x32 portrait OLEDs across the
   physical desk gap without increasing restlessness?
5. What is the smallest physical spike that can validate the riskiest unknown
   before broad implementation?
