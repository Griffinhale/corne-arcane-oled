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
- The measured headroom is small enough to be the first thing any firmware idea
  is checked against. The release image sits about 4 KB under the flash ceiling
  and a few hundred bytes under the static-RAM growth gate. Anything that needs
  more room than that is an off-keyboard idea until something else is removed.
- Budget figures move with the compiler. The documented table was measured with
  one ARM toolchain and continuous integration measures another, so the two
  disagree by roughly 700 flash bytes. Pin the compiler before treating either
  number as a gate.
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

## What a first stable version would mean

A first stable version is not more platforms. It is that somebody who is not
the author can build this, flash it, run it, and understand it, on the hardware
it already targets. Four things stand in the way, and each has a precondition
that is a decision, not an implementation.

- Settle the health-pip question. Every build still produces two candidate
  images that the release workflow has to actively exclude, which means an
  unresolved product decision is baked into continuous integration. The
  precondition is visual coverage of every pip count in both geometries, since
  the catalog currently carries three of them.
- Pin the ARM toolchain in continuous integration, because a budget gate that
  moves with the compiler is not a gate.
- Make the shared endpoint bearable. The safe launcher already hands the one
  Raw HID endpoint between the daemon and Vial correctly, so the problem is
  that it is a command line incantation and not that it is wrong. A small
  status surface that shows link state, opens Vial in one action, and displays
  an observation window would absorb most of this without touching firmware. A
  second HID interface for diagnostics would be cleaner and needs a USB
  endpoint budget before it is worth costing.
- Watch one other person flash a board from `flashing.md` alone. Everything
  else about the documentation is inference until that has happened once.

## Catalog coverage

The visual catalog is host-side and never flashed, so scenes cost review time
and golden hashes but no firmware budget. The gaps below are combinations the
renderer supports and nothing currently pins.

- Health renders at three pip counts out of nine, always with both wizards
  equal. The full ladder and a few asymmetric pairs are the precondition for
  settling the health-pip question.
- Status effects render at one intensity each. Intensity is load-bearing
  because it feeds wind-up length, so its presentation should be pinned at more
  than one value.
- Wind-up has one frame per form and no progression, so the progress indicator's
  range is unpinned. Sampling one long wind-up across its span covers the state
  a player spends the most time looking at.
- No scene has both wizards acting at once, and two spells in flight is a legal
  state. The per-side matrix covers each half alone.
- No scene has two fields active at once, and there are exactly two slots, so
  slot interaction and combined silhouettes are unpinned.
- The lifecycle arc is covered on one half at one roster variant. The mirrored
  arc, a spell fizzling at a downed wizard, and each replacement variant are
  not.
- Every combat scene uses the dawn sky. The renderer composites a duel over
  four sky phases and a sixteen-step celestial arc, and nothing pins that
  pairing at any other hour.
- A review that a person cannot actually sit through stops being a review.
  Growing the catalog needs a stated ceiling before it needs more scenes.

## Off-keyboard presentation

Deferred until the keyboard version is calm in real work, and listed here
because the constraint above makes the keyboard the wrong place for any of it.

- The simulation is hardware-agnostic and already runs natively in the test
  harness, so a desktop window showing both canvases is mostly plumbing over
  code that exists. It would also cover a keyboard with no display and both
  non-split cases, and give any later platform a reference implementation.
- A desktop version that read typing would invert the privacy position rather
  than extend it. On the keyboard, positions never leave the firmware. On a
  desktop, sampling key positions across the session is exactly the access this
  project refuses. Devices without a keyboard should therefore run the city and
  not the duel, driven by the enum stream the daemon already produces, with no
  keystroke capture anywhere.
- A gamepad is the one input other than a keyboard that could honestly duel,
  because its input is local to the application and carries nothing private.
- Porting the daemon is new focus producers and a new HID transport, not a
  rewrite, because semantic policy is already separate from the code that
  monitors a bus. The privacy boundary has to hold identically on any new
  platform or the port is not worth having.

## One world across several devices

An open question, not a plan. Recorded because it interacts with the
authority rule above.

Determinism is what would make it possible. A fixed-tick integer simulation
with a compact wire format is what lets separate devices agree without a
server. The difficulty is authority, and this repository already answers that
question one way: one master decides and everything else draws.

The cheap reading keeps that answer. One device owns the world and the others
are windows onto it, each showing a different aspect, which makes this a
presentation problem and preserves every existing invariant. The expensive
reading gives several devices authority over parts of one universe and needs a
synchronisation protocol, clock discipline, and reconciliation, none of which
exist here. The second is a research project and should be named as one.

A deeper city simulation also wants needs modelling, pathfinding, entity pools,
and persistence, all four of which the scope guards above forbid. Off the
keyboard those guards do not bind. On it, lifting them would be the largest
product decision this project has made.
