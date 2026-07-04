# Nav pathfind throttle steers a stale bearing into terrain

## Context

Commit `cd8ea6d5` ("Perf") added a pathfind throttle to `PlayersPostRender::ComputeNavigation`
(`PlayersNavigation.cpp`). `engine::NavQueryDirection` now runs only when `bRecompute` is true — every
`kiNavRecomputeInterval` (16) ticks, staggered per player by `globalId`, plus immediately on a mode
change, destination change, or direction re-seed. Between recomputes the cached `rVecAiDirection` — a
normalized **world-space** bearing toward the path's first waypoint (typically an obstacle corner) — is
reused unchanged and handed to `ApplyMovement`.

At the 32 Hz tick rate that is up to **0.5 s / ~16.7 world units** (`kfPlayerMaxSpeed` = 33.33 u/s) of
straight flight on a bearing that no longer tracks the ship. A ship rounding an island corner keeps
steering the old bearing, sails **past** the corner, and drives into the island behind it — the reported
"spaceships deliberately flying into terrain". The debug nav line (`pVecDebugNavWaypoints[i]`, written
only on recompute ticks and carried forward otherwise in the `Players.cpp` Update loop) still points at
the stale corner, so it visibly "leads directly into the yellow exclusion area". This is not the escape
algorithm reversed: a bearing that was *skirting* the corner becomes a bearing *into* the island the
moment the ship passes the corner's tangent.

The NavQuery / NavBuild / NavCellData geometry, A*, LOS broad-phase, and escape/snap logic were all
reviewed and are correct — the recent nav commits (`2be8635b`, `6fb50126`, `e1feb056`, `fc91fb40`) are
pure refactors and the `cd8ea6d5` broad-phase is sound. The only regression is the throttle reusing a
stale steering vector.

A temporary diagnostic (`kbDiagNavStale` block at the end of `ComputeNavigation`, plus
`kfNavStaleLookahead` and the per-branch `vecActiveDestination` capture) was added this session to
confirm the mechanism from a capture. Its `age`/`cos` fields distinguish throttle-staleness (`age > 0`,
`cos << 1`) from a hypothetical NavQuery LOS bug (`age == 0`). Removing that scaffolding is part of this
fix's diff.

## Design

**Option A — terrain-triggered forced recompute (recommended).** In `ComputeNavigation`, before the mode
branches, probe terrain a short distance along the carried `rVecAiDirection`:
`vecLookahead = position + kfLookahead * rVecAiDirection`; if
`engine::gpIslandTerrain->FrameElevation(rStaticData, vecLookahead) >= gBaseHeight - kfPlayerRadius - kfPushMargin`,
force `bRecompute = true`. This re-paths precisely when the held bearing would drive into terrain (i.e.
right after passing a corner), restoring pre-throttle behavior only where it matters while keeping the
16-tick throttle for open-water flight. No new SOA field and no `kiVersion` bump. Deterministic:
`FrameElevation` is a shared read of the deterministic per-cell elevation grid, the trigger uses only
shared state (carried `rVecAiDirection`, position, navData/grid) so client and server force-recompute on
the same ticks, and it draws no RNG (the mode-4/5 RNG draws stay unconditional, outside the throttle).
Worst case near a genuine shore it degrades to every-tick recompute for that ship — exactly the correct
pre-throttle behavior, cost bounded to ships hugging terrain.

**Option B — shared-waypoint tracking.** Promote the current path waypoint from the client-only debug
field (`pVecDebugNavWaypoints`) to a shared SOA member; each tick steer toward
`normalize(waypoint - position)` (cheap) and re-run the full A* (`NavQueryDirection`) only on the cadence
or when within R of the waypoint. Most faithful to "follow a path between recomputes", but adds a shared
collection member → Players `kiVersion` bump → `Frame::kiVersion`, serialization, CRC, and
`LogDifferences` (the full add-collection-member checklist). Heavier and determinism-sensitive.

**Option C — shorten `kiNavRecomputeInterval`.** One-line band-aid (e.g. 16 → 4 ≈ a 4.2 u stale window);
4× the A* cost and still overshoots at high speed. Not recommended on its own.

Recommend **Option A**. Confirm against the `kbDiagNavStale` capture before landing.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/PlayersNavigation.cpp` — the
  `PlayersPostRender::ComputeNavigation` throttle (`bRecompute`, `kiNavRecomputeInterval`,
  `rVecAiDirection`). Option A adds the terrain-lookahead trigger here and deletes the temporary
  `kbDiagNavStale` / `kfNavStaleLookahead` / `vecActiveDestination` diagnostic scaffolding.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — the Update loop that calls
  `ComputeNavigation` then `ApplyMovement(iNavDirection, vecAiDirection, …)` and carries the debug
  waypoint forward. Read-only for Option A; Option B stores/loads the new shared waypoint field here.
- `Engine/Source/Frame/IslandTerrain.h` — `IslandTerrain::FrameElevation` (the trigger probe; already the
  same query `PlayersPostRender::ApplyTerrainPush` uses).
- (Option B only) `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h` collection
  struct + every add-collection-member site.

## Out of scope

- Any change to `NavQuery` / `NavBuild` / `NavCellData` geometry, A*, LOS, `SegmentBlockedByObstacle`, or
  the broad-phase acceleration — reviewed and correct.
- Enemy `SpaceshipsPostRender::AvoidTerrain` / `game::ComputeTerrainAvoidance` — a separate obstacle
  system; enemy spaceships carry no nav line and are not implicated.
- The mode-4/5 RNG-draw-parity contract — must stay untouched; the fix must not move any `common::Random`
  draw inside the throttle.
- `Frame/FlagshipLossNavFallback.md`'s mode-5 no-flagship exit — a distinct defect in the same function;
  co-schedule, do not absorb.

## Acceptance criteria

- A ship steering toward an obstacle corner re-paths before entering the exclusion polygon; no sustained
  penetration of nav obstacle polygons during normal island navigation.
- The `kbDiagNavStale` diagnostic (while still present) stops logging `age > 0, cos << 1`
  stale-into-terrain events under the same scenario.
- Client and server stay CRC-identical — no new desync; the replay/determinism check passes.

## Notes

- **State invariant exposure.** Option A touches the deterministic PostRender sim path (steering →
  position → CRC) but changes **no** serialized layout, `kiVersion`, wire format, or RNG consumption — it
  only conditionally forces an already-existing recompute from shared reads. Option B changes SOA layout
  → `kiVersion` bump + serialization + CRC + `LogDifferences` (add-collection-member).
- **Grill decision (`/external-grill-plan`):** Option A (terrain-triggered forced recompute) vs Option B
  (shared-waypoint path tracking) — perf-locality and zero version churn (A) vs path fidelity between
  recomputes at the cost of a version bump and a new shared field (B). Recommend A unless faithful
  path-following between recomputes is explicitly wanted.
- This is the fix targeted by the temporary `kbDiagNavStale` diagnostic added this session; deleting that
  block is part of the fix diff.
