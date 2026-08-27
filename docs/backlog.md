# Corne Arcane exploration backlog

What is listed here is deferred, not planned. The split v12 world, the eight
districts, the magic signatures, and the host adapters are all implemented and
running; none of that is backlog. Everything below is an idea with a stated
precondition, and the precondition is usually that something already built has
proven calm in real work, not merely correct in tests.

## Presentation worlds and information surfaces

- Consider further art only after the eight districts, four-stage Observatory,
  almanac, crowds, and field silhouettes have been read at desk distance over a
  full working day, not just confirmed legible once.

## Input-driven fiction and combat variety

- Explore additional bounded spell outcomes only after the two-spell/two-field
  candidate proves calm and readable in physical work sessions.
- Preserve one authoritative master simulation, hard entity/chain caps,
  deterministic fixed ticks, and typing-path independence.

## Host integration

- Consider adapters beyond KWin/GNOME and Firefox only behind the same
  fail-soft, opt-in, enum-only privacy boundary.

## Architecture constraints for future work

- The split v12 packet is exactly 32 bytes and has no wire reserve.
- Raw HID remains a fixed 32-byte semantic protocol and shares the Vial
  interface. The safe launcher hands that one endpoint between the daemon and
  Vial; a future integration must preserve exclusive ownership.
- Current resource figures are in `development.md`. Future content needs an explicit
  flash budget and must preserve the 88 KiB ceiling and 8 KiB hard-stop reserve.
- Static RAM growth, stack headroom, split/OLED timing, no-allocation gates,
  power policy, stale-link recovery, and exact visual/determinism tests remain
  release gates.
- No permanent progression, save files, long-lived host database, unrestricted
  entity pool/pathfinding/needs simulation, host-supplied text or bitmaps, or
  streamed framebuffers without an explicit product-level reversal of the
  current scope guards.

## Questions to settle before coding

1. Which direction adds the most ambient value after Observatory acceptance:
   deeper existing cities, richer spells, or richer scry/host semantics?
2. Should the next version spend protocol bytes, flash reserve, or both, and
   what recovery artifact and hard stop protect the accepted 0.4 baseline?
3. Which additions are authoritative mechanics versus disposable presentation
   or external context?
4. How will each idea remain readable on two 128x32 portrait OLEDs across the
   physical desk gap without increasing restlessness?
5. What is the smallest physical spike that can validate the riskiest unknown
   before broad implementation?
