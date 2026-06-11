# Missiles: Remove Dead Per-Element pfExplosionRadii SOA Field

## Context

The Missiles collection carries a per-element explosion-radius SOA column,
`MissilesPostRender::pfExplosionRadii` (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/
Missiles.h:118`), that is **write-only** — it is never read to drive any behavior. Confirmed by grep across the
collection:

- **Declared** `:118` (`float* __restrict pfExplosionRadii`).
- **In `SharedMembers`** `:132` → it participates in the **shared CRC** (the client/server desync hash) and in
  save/load.
- **Zeroed at spawn** `Missiles.cpp:460` (`rCurrentPostRender.pfExplosionRadii[iIndex] = 0.0f;`).
- **memcpy'd forward** each tick in `AllocateAndCopy` `Missiles.cpp:312`.
- **Compared in `LogDifferences`** `Missiles.cpp:567`.

There is **no read site** that consumes the value for gameplay. The actual missile area-of-effect radius comes
from a **`HealthDamage.h` constant** (per the report's Missiles note: "AoE radius is the `HealthDamage.h`
constant (per-element field dead — zeroed, never read, yet CRC'd)"), not from this array. So `pfExplosionRadii`
is dead state that nonetheless:

1. Costs SOA memory and a per-tick `memcpy` for every missile.
2. **Participates in the shared CRC** — a zero-valued field that always matches, but still adds bytes to the
   hash and a `LogDifference` line to desync diagnostics for no reason.

Removing it shrinks the missile SOA, removes a per-tick copy, and — because it is in `SharedMembers` — **bumps
the Missiles collection `kiVersion`**, which propagates into `game::Frame::kiVersion` (the sum-of-versions in
`Frame.cpp`). That invalidates existing saves/replays (expected and acceptable for a layout change).

## Design

Remove the field following the **add-collection-member checklist in reverse** (the canonical procedure for any
change to a collection's memory layout — invoke the `add-collection-member` skill at execution; it enumerates
every site that must move in lockstep for compilation, layout, and deterministic-CRC correctness). The deletion
touches, at minimum:

1. **Declaration** — drop `pfExplosionRadii` from `MissilesPostRender` (`Missiles.h:118`).
2. **`SharedMembers` tuple** (`Missiles.h:132`) — remove the `rSelf.pfExplosionRadii` entry. This changes the
   shared-CRC byte layout.
3. **Allocation / pointer-table wiring** — wherever the SOA pointers are sized/assigned for `MissilesPostRender`
   (the collection's allocate/capacity machinery; the skill identifies the exact site), drop the field's slot.
4. **`AllocateAndCopy` memcpy** (`Missiles.cpp:312`) — remove the forward-copy line.
5. **Spawn zero-init** (`Missiles.cpp:460`) — remove the `= 0.0f` store.
6. **`LogDifferences`** (`Missiles.cpp:567`) — remove the `common::LogDifference<"pfExplosionRadii">` line.
7. **`kiVersion` bump** — bump `MissilesPostRender::kiVersion` (the layout-version constant the checklist
   requires on any SOA layout change) so `Frame::kiVersion` moves and stale saves/replays reject cleanly.

Verify (the skill's job, but call it out) there is genuinely **no read site** before deleting — a single grep
for `pfExplosionRadii` should show only the six write/declare/CRC/log sites above and nothing that reads it into
gameplay math. If an unexpected reader surfaces, this becomes a "wire the constant in / keep the field" decision
instead — but the evidence is that the AoE comes from `HealthDamage.h`.

## Out of scope

- The `HealthDamage.h` AoE-radius constant that is the *real* radius — unchanged; this plan removes the unused
  parallel field, it does not touch the working radius source.
- Any other Missiles SOA field (`pfExhaustLengths`, `pfPitches`, etc.) — only `pfExplosionRadii` is dead.
- The missile area-damage *behavior* (`engine::AreaDamage` integration) — unchanged; it already uses the
  constant, not this field.
- Other collections' dead-field candidates (Explosions `pfLightPercents`/`pfSmokePercents`, Players, etc.) —
  those are separate findings with their own cleanup plan.
- Converting `pfExplosionRadii` to a real per-missile radius (would be a *feature*, not debt) — out of scope;
  the radius is a balance constant by design.

## Acceptance criteria

- `pfExplosionRadii` is gone from the declaration, `SharedMembers`, allocation wiring, `AllocateAndCopy`,
  spawn, and `LogDifferences` — a repo grep for the symbol returns nothing.
- `MissilesPostRender::kiVersion` is bumped; `Frame::kiVersion` changes accordingly (stale saves/replays reject
  at load, as expected for a layout change).
- Client and server build clean; missile area damage still applies at the `HealthDamage.h` radius (no gameplay
  change — the removed field never affected it).
- A client/server session stays in CRC sync (the shared hash now excludes the removed field on both sides).

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.h` — `MissilesPostRender`
  declaration (`:118`) and `SharedMembers` (`:132`); the `kiVersion` constant to bump.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — `AllocateAndCopy` memcpy
  (`:312`), spawn zero-init (`:460`), `LogDifferences` (`:567`), and the SOA allocation/pointer-table site.
- `Projects/BrokenEngineSandbox/Source/Frame/HealthDamage.h` — the real AoE-radius constant (read-only
  reference, confirms the field is redundant).
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` — `Frame::kiVersion` sum (read-only; it picks up the
  Missiles `kiVersion` bump automatically).

## Notes

- **Determinism/CRC exposure:** removing a `SharedMembers` field changes the shared-CRC byte layout and bumps
  the collection `kiVersion` → `Frame::kiVersion`. This invalidates existing saves and locally-recorded replays
  (clean rejection at load — same accepted class as other layout changes). Client and server must be rebuilt
  together.
- **Execute via the `add-collection-member` skill** (run in reverse for removal) — every step affects
  compilation, memory layout, or CRC, so partial completion causes subtle desync.
- Low risk despite the CRC touch: the field is always zero, so removing it cannot change any *value* in the
  hash — only the layout. The bump is the safeguard that keeps old/new builds from silently mismatching.
