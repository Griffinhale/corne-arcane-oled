# Architecture

Corne Arcane has one authoritative simulation and two local presentation
surfaces. The master half decides mechanics; the slave half never reconstructs
or advances authoritative world state. Both halves draw locally from bounded
projections, so the split carries state rather than pixels.

## Firmware data flow

```text
physical matrix
    │
    ├─> ordinary QMK key output
    │
    └─> sampled positions + bounded edge queue
             │
             v
       master sim_tick (25 Hz)
             │
             ├─> model -> view -> split snapshot v12 -> slave presentation
             │
             ├─> render projection -> scene compositor -> local OLED
             │
             └─> world-owned RGB policy
```

`keymap.c` owns hardware integration, wall-clock sampling, split transport, and
OLED/RGB hooks. It supplies physical positions—not keycodes or text—to
`duel_sim.c`. The simulation orchestrator preserves the fixed phase order
declared in `duel_sim_internal.h`. `duel_incantation.c` owns collection and
descriptor compilation; `duel_combat.c` owns damage, wards, collision, motion,
status, residue, fields, derived magic signatures, echo/bloom, aftermath, and
lifecycle mechanics. Stable layouts and enum values live in `duel_model.h`.

`duel_view.c` creates the canonical presentation projection. `duel_proto.c`
packs that view plus synchronized presentation state into the split snapshot.
The slave validates version, ranges, reserved bits, and CRC before accepting a
snapshot; it does not derive a world from the packet.

Rendering proceeds through `duel_render_t`. `duel_framebuffer.c` owns clipped
1bpp pixels, lines, and mirrored desk-space geometry. Environment, combat, and
overlay drawing are isolated in their respective modules; `duel_draw.c` owns
only full-scene composition order. Resident, courier, and event derivation
depend on civic/render/framebuffer contracts, never on the compositor.

## Host data flow

```text
desktop adapters + explicit event command
                 │
                 v
       privacy-bounded semantic state
                 │
                 v
         Raw HID v3 heartbeat
                 │
                 v
        master disposable context
                 │
                 v
          split v12 propagation
```

`dbus_contract.py` owns public names, paths, interfaces, XML, methods, and
repository-state values. `dbus_services.py` implements the public services.
`adapters.py` is pure semantic policy; `dbus_adapters.py` owns monitoring,
property unpacking, reconnects, and fail-soft integration. `runtime.py` owns
GLib deadlines, wake coalescing, polling, semantic revision detection,
heartbeat dispatch, and deterministic cleanup. `daemon.py` only constructs
dependencies, acquires the bus, and starts the runtime.

Every Raw HID write is a request/response exchange. A valid VIA echo must arrive
before the next heartbeat is scheduled. A timeout, mismatch, short read, or
device error closes the transport and begins rediscovery with a fresh session.
Vial shares that endpoint, so `corne-arcane-vial` performs an exclusive,
state-preserving service handoff. Diagnostics use the same ownership guard for
single reads and observation windows, restoring only a service that was active
beforehand. Their separate diagnostics-only v2 protocol does not accept or
provide compatibility with the prior production Raw HID v2.

Zsh, Bash, and Fish hooks emit only monotonic duration, integer exit status,
and normalized repository state. KWin and the opt-in GNOME extension report
only application/desktop identifiers. The optional Firefox bridge carries only
browser event kind and intensity; it has no URL, title, history, content, form,
referrer, or typed-text channel. Missing buses, extensions, permissions, or
native hosts disable only their adapter.

## Invariants

- Authority: only the master advances combat and shared world state. The slave
  renders validated projections.
- Determinism: simulation uses fixed 40 ms ticks, integer math, explicit state,
  fixed phase ordering, and no time reads inside mechanics.
- Allocation: firmware simulation, encoding, and rendering allocate no memory.
  Event storage stays caller-visible to avoid a second stack copy.
- Privacy: host inputs are normalized to enums, counters, flags, and salted
  digests. Titles, bodies, commands, paths, URLs, filenames, and typed text do
  not enter retained semantic state or either wire protocol.
- Timing: host state expires after 1.5 seconds; heartbeat and reconnect timing,
  display sleep, 25 Hz simulation, and 20-second HP regeneration are contracts.
- Protocols: production Raw HID v3 and split v12 are exactly 32 bytes. The
  diagnostics-only v2 reports are three 32-byte pages with an 18-byte reverse
  split reply. Versions, enum values, packing, reserved bits, CRC coverage, and
  stale fallback are stable.
- Stale link: invalid or absent split traffic selects safe local presentation.
  The next valid snapshot replaces synchronized state directly, without replay.
- Power: only physical key activity wakes OLED and RGB surfaces. Host or
  background-world changes do not wake sleeping hardware.

## Dependency direction

Dependencies point from integration toward pure contracts:

```text
QMK glue -> runtime/protocol/render entry points
sim orchestration -> model + incantation + private combat phases
view/protocol -> model (never drawing)
scene compositor -> render + framebuffer + drawing subsystems
drawing subsystems -> render + framebuffer + civic/view contracts
host daemon -> runtime -> services/adapters/heartbeat -> pure semantic/protocol policy
```

Lower layers must not include QMK, GLib, D-Bus, hidraw, or compositor details.
Protocol and model headers must not depend on drawing. Presentation modules may
read projections but must not decide mechanics. Any exception should be treated
as an architectural change and reviewed against the invariants above.
