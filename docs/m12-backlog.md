# M12 expansion backlog (gated stub)

No M12 expansion code belongs in M11.5. Planning and implementation may begin
only after `docs/m11.5-acceptance.md` is signed off and its physical measurements
show sufficient flash, RAM, split bandwidth, recovery, and timing headroom.

Deferred candidates:

- Additional application worlds and dense scry pages.
- New actors, medic variants, spell families, and outcome grammars.
- More Archive objects or higher-density ambient behaviors.
- New host semantic categories beyond the fixed v2 privacy-redacted summary.

Each candidate must preserve typing independence, deterministic fixed-tick
mechanics, privacy boundaries, Raw HID/split compatibility, and the accepted
M11 protected-region and power-policy contracts, the M11.5 canonical view, and
the five explicitly unallocated v8 bytes.
