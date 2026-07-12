# Swept Ship Terrain Collision

## Context

`game::TracePointAgainstTerrain` (`Projects/BrokenEngineSandbox/Source/Frame/TerrainUtils.cpp:132-275`) traces only a point through the piecewise-constant elevation grid. Players and Spaceships already register their true previous-to-current motion segments and scalar body radii in `PlayersPostRender::PreCollision` (`Players.cpp:676-730`) and `SpaceshipsPostRender::PreCollision` (`SpaceshipsCombat.cpp:98-148`), but terrain is not part of those timelines. Their current terrain responses sample only the integrated endpoint: `PlayersPostRender::ApplyTerrainPush` (`PlayersNavigation.cpp:457-468`) changes velocity after an elevation threshold, while `SpaceshipsPostRender::ApplyTerrainBounce` (`SpaceshipsNavigation.cpp:118-134`) detects center penetration, moves the ship laterally, and reflects velocity. A body radius can therefore overlap a height step before its center does, and a sufficiently large or fast ship can cross a blocking cell between endpoint samples.

This is the user-approved follow-up from the collision-semantics work: projectiles remain point traces, while Players, Spaceships, and future large ships gain deterministic swept-volume terrain contact. It is queued separately because shape representation and ship response change CRC'd gameplay semantics beyond projectile correctness.

## Design

Resolve these two architectural decisions in `/external-grill-plan` before implementation; do not let the geometry choice silently dictate gameplay response.

1. **Terrain sweep shape.** Recommended A: use a conservative sphere proxy for Players and Spaceships, taking `kfPlayerRadius` / `kfSpaceshipRadius` from the same source already bound to entity collision, and expose one collection-independent swept-sphere query that future large-ship collections can call. This matches current scalar sizing, adds no SOA/layout state, and keeps point projectiles on `TracePointAgainstTerrain`. Option B: add a yaw-oriented box query with fixed per-collection half-extents and previous/current orientation; define whether rotation during the tick is swept exactly or conservatively before coding it. Do not add both representations speculatively. If a future hull needs tighter-than-sphere contact, extend the selected query deliberately rather than inferring render bounds.
2. **Contact response and navigation.** Recommended A: terrain is an exclusive motion cutoff for that tick. Preserve entity hits strictly before the terrain TOI, reject entity candidates at or after it using the existing maximum-time binding, place the ship at a non-penetrating contact center, then retain collection identity: Players remove inward motion and apply their gentle terrain push/slide; Spaceships retain bounce rotation and reflected/outward velocity using the contact normal. Option B: treat the sweep only as an early warning for existing endpoint response; this does not guarantee non-penetration and must not be selected if hard collision is the acceptance goal. In either option, decide exact TOI ties with terrain before entity and frame transfer, and keep `ComputeTerrainAvoidance` as predictive steering rather than a collision response.

After those choices:

- Add a hand-rolled, allocation-free query beside `TracePointAgainstTerrain` in `TerrainUtils`. Traverse only elevation cells whose footprints can intersect the swept shape, return the earliest normalized TOI, non-penetrating center, and deterministic contact normal, and define start-overlap, grid-edge/corner, zero-motion, exact-tie, and end-point ownership. Reuse the elevation sampler's floor-grid ownership and arithmetic order. Jolt may be read for conceptual inspiration only; copy no source and add no dependency or notice.
- In Player and Spaceship `PreCollision`, trace the same previous/current endpoints and `[0,1]` interval used by the engine collision layer. Combine terrain TOI with frame-exit TOI into the layer's exclusive maximum time without shortening the object's interpolation window. Retain per-object terrain hits in phase-safe thread-local scratch through `PostCollision`.
- In `PostCollision`, apply any earlier entity results, then apply the chosen terrain response when terrain precedes transfer. Suppress transfer for a terrain-clamped ship; preserve transfer when frame exit is earlier. Update current Interpolate position and PostRender velocity/direction state symmetrically on client and server, with position W=1 and direction/velocity/normal W=0.
- Convert Players and Spaceships together so their timing, tie, and transfer rules cannot drift. Keep shape-specific math in `TerrainUtils` and collection-specific response constants/behavior in the existing navigation TUs.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/TerrainUtils.h` / `.cpp` — `SegmentHit`, `TracePointAgainstTerrain`, `FrameElevationSampler`-aligned traversal, and the new swept-volume terrain query.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.h`, `Players.cpp`, `PlayersCombat.cpp`, `PlayersNavigation.cpp` — `kfPlayerRadius`, collision scratch and cutoff binding, `PostCollision`, and `ApplyTerrainPush` response.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.h`, `SpaceshipsCombat.cpp`, `SpaceshipsNavigation.cpp` — `kfSpaceshipRadius`, collision scratch and cutoff binding, `PostCollision`, `ApplyTerrainBounce`, and `AvoidTerrain` separation.
- `Engine/Source/Frame/Collision.h` / `.cpp` — existing explicit motion intervals and exclusive `pfMaxTimes` contract; change only if the selected terrain tie policy cannot be expressed by that contract.
- `Documents/FloatingPointDeterminism.txt` and affected `AGENTS.md` files — determinism contract and durable terrain/collision semantics.

## Out of scope

- Projectile terrain shapes: Blasters and Missiles remain point-based.
- General rigid-body physics, impulses, angular dynamics, ship-to-ship OBB collision, or a third-party physics dependency.
- Deriving collision bounds from client render meshes or packed assets.
- Terrain avoidance/pathfinding redesign; only reconcile existing avoidance/push/bounce with authoritative contact.
- Unit tests or agent-harness extensions for arbitrary terrain/projectile placement.

## Acceptance criteria

- Players and Spaceships cannot tunnel through or end penetrated in a blocking piecewise-constant terrain cell when their selected volume, not merely their center, crosses it during a tick.
- Start overlap, zero motion, exact grid-edge/corner crossings, simultaneous terrain/entity contact, and terrain/frame-exit ties have one documented deterministic outcome; terrain wins exact ties against entity and transfer.
- Terrain, entity collision, and frame transfer use the same explicit motion interval. Entity hits before terrain remain visible; hits at or after the exclusive terrain cutoff do not; no terrain-clamped ship also transfers.
- Player response preserves the intended gentle push/slide behavior, Spaceship response preserves bounce/turn behavior, and predictive terrain avoidance remains distinct from authoritative contact.
- Client and server builds pass; replay determinism passes across repeated runs; final-code proofs cover traversal ownership, TOI ordering, W lanes, phase-scratch lifetime, and absence of tick-path heap allocation.
- Agent-harness smoke drives normal Player/Spaceship movement near islands and reports no crash, desync, transfer/contact double-resolution, or persistent penetration. Record exact synthetic edge arrangements as unautomated if the current harness cannot place them.

## Notes

- **Revisit when:** before increasing `kfPlayerRadius` / `kfSpaceshipRadius`, before adding a larger ship collection, or when a reproducible radius-overlap/tunneling case is observed. The current conversion remains part of this plan once scheduled.
- **Determinism/CRC:** high exposure. Interpolate positions and PostRender velocity/direction/flags feed shared simulation and CRC; preserve `/fp:strict`, fixed arithmetic order, deterministic tie-breaks, and client/server parity. Do not rely on a third-party query whose cross-build bit identity is undocumented.
- **Layout/versioning:** prefer transient thread-local scratch and existing constants. No `Collection` member, `kiVersion`, save/replay format, wire protocol, `.pack`, shader, or client/server guard change is expected. Reassess and explicitly version if the selected shape requires persistent SOA state.
- **Allocation:** collision phases are allocation-tracked hot paths. Retained scratch may grow only under the repository's established suppressed, capacity-retaining pattern; the geometric traversal itself must allocate nothing.
- **Prerequisite:** the engine's explicit collision interval / exclusive cutoff contract and the point DDA are present-state foundations for this feature.
