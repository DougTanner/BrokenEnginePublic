# Scratch Accessor Parity

## Context

Findings originate from a second-pass multi-agent review and are unverified claims; the executing agent must confirm each one before editing.

Commit 1c48463a simplified the engine's double-`static thread_local` deferred-construction scratch accessor: the `GetCollisionEventScratch` free function in `Engine/Source/Frame/Collision.cpp` (`:38-44`) is now a single `static thread_local CollisionEventScratch sScratch; return sScratch;` with the comment "default construction is allocation-free (empty vectors) … Growth sites suppress tracking" — the nullptr-guarded `spScratch` indirection and its `ScopedSuppressAllocationTracking` construction wrapper were dropped because default construction of the all-`std::vector` scratch struct allocates nothing (verified by that commit's full replay determinism pass).

The identical old pattern remains verbatim in four game sim TUs, each guarding an all-`std::vector` scratch struct behind the same two-`static thread_local` dance the engine no longer uses:

- `GetBlasterCollisionIntervalScratch` — `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/BlastersUpdate.cpp:43-54` (struct `BlasterCollisionIntervalScratch`, `:34-41`: five `std::vector` members)
- `GetMissileCollisionIntervalScratch` — `.../Missiles/MissilesUpdate.cpp:32-43` (struct `MissileCollisionIntervalScratch`, `:23-30`: five `std::vector` members)
- `GetPlayerCollisionIntervalScratch` — `.../Players/Players.cpp:664-675` (struct `PlayerCollisionIntervalScratch`, `:657-662`: three `std::vector` members)
- `GetSpaceshipCollisionIntervalScratch` — `.../Spaceships/SpaceshipsCombat.cpp:38-49` (struct `SpaceshipCollisionIntervalScratch`, `:31-36`: three `std::vector` members)

The engine and game siblings now diverge, against the "mirrored patterns stay parallel" directive. The deferred construction existed only to dodge main-loop allocation tracking (`Engine/Source/Memory/AGENTS.md`: `thread_local` construction can run before allocator startup, and untracked-suppressed allocation in the main loop trips `DEBUG_BREAK`); when default construction allocates nothing there is nothing to defer, and each accessor's caller already wraps the actual `.resize()` growth in `ScopedSuppressAllocationTracking` with a `// Heap:` comment (e.g. the `PlayersPostRender::PreCollision` suppress at `Players.cpp:680-682`).

Note the parity target is the accessor shape only — 1c48463a also folded two file-scope counters into `CollisionEventScratch`, but the four game scratch structs have no analogous file-scope counters, so no field migration applies here.

## Design

1. **Verify first** (per Diagnosis Discipline; refuted claims are dropped and named as residuals). Specifically: confirm each of the four accessors still matches the old Collision.cpp pattern (nullptr-guarded `spScratch` + inner `static thread_local` under `ScopedSuppressAllocationTracking`), and confirm each scratch struct's default construction is allocation-free — all members `std::vector` or similarly non-allocating when default-constructed — before simplifying. The deferred construction existed for allocation tracking; if any struct gains a default-allocating member between now and execution, that site is not eligible and is surfaced as a residual instead.
2. Apply the same simplification 1c48463a applied in Collision.cpp to the four game accessors: collapse each to a single `static thread_local <Struct> sScratch; return sScratch;` with the Collision.cpp comment shape ("Function-local TLS defers construction until first use; default construction is allocation-free (empty vectors) … Growth sites suppress tracking"), keeping the four sites byte-parallel with each other and with `GetCollisionEventScratch`'s shape. Caller-side growth suppression (`// Heap:` + `ScopedSuppressAllocationTracking` around the `.resize()` calls) is already in place and stays untouched.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/BlastersUpdate.cpp` — `GetBlasterCollisionIntervalScratch` (`:43-54`)
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesUpdate.cpp` — `GetMissileCollisionIntervalScratch` (`:32-43`)
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — `GetPlayerCollisionIntervalScratch` (`:664-675`)
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/SpaceshipsCombat.cpp` — `GetSpaceshipCollisionIntervalScratch` (`:38-49`)
- `Engine/Source/Frame/Collision.cpp` — `GetCollisionEventScratch` (`:38-44`), read-only reference for the target shape

## Out of scope

- Any behavior or stored-value change — this is accessor shape only; the scratch structs' contents, growth sites, and consumers are untouched.
- The collision algorithm (interval building, layer registration, sweep resolution) in any of the five TUs.
- Any other cleanup in those TUs — the neighboring file-scope `thread_local` collision-flag/radius/damage vectors, constants, or unrelated patterns stay as they are.
- Folding counters or other fields into the scratch structs (the Collision.cpp counter-fold half of 1c48463a has no game analogue).

## Acceptance criteria

- Every executed item has a recorded verification result preceding its change (pattern match confirmed; default construction confirmed allocation-free per struct).
- CRC neutrality — structural change only, verified with an agent-harness replay determinism check: the same replay, run on pre- and post-change builds, produces identical per-tick CRCs.
- No `Frame::kiVersion` bump — if verification or the replay check shows any stored value shifts, stop and surface rather than bumping.
- Client and server both compile.

## Notes

- **Invariant exposure**: touches CRC'd sim TUs with intended CRC-neutral edits, and allocation-tracked paths (the accessors run inside the main loop's PreCollision fan-out) — the verify-first allocation-free check is what keeps the `ScopedSuppressAllocationTracking` removal safe. No wire, save, or `.pack` change; no client/server guard-scope change.
- **Warning-only overlap**: live Frame-area plans share these TUs (`Frame/TransferSentinelConflation.md`, `Frame/MissileLifetimeAndTargetLifecycle.md`, `Frame/PlayerTransferUuidPreservation.md`, `Frame/FireCooldownNegativeFloor.md`, `Frame/MissileImpactTrailPhaseSeparation.md` cite `Players.cpp`/`MissilesUpdate.cpp`/`BlastersUpdate.cpp`/`SpaceshipsCombat.cpp`) — refresh citations if one lands mid-flight. Several of those plans batch a consolidated `Frame::kiVersion` bump; this plan is CRC-neutral and must not join any `kiVersion` batch.
- Mechanical once verified; no open decisions. Client and server land together.
