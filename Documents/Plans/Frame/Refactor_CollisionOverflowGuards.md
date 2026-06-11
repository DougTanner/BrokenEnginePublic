# Refactor: Collision/AreaDamage Overflow-Path Guards

## Context

Source: /external-refactor-clean on `Engine/Source/Frame` (non-recursive). The thread_local lazy-init pattern in `Frame/CLAUDE.md` requires first-use resize **and overflow growth** to wrap reallocs in `ScopedSuppressAllocationTracking`; five overflow branches don't, so after the intended `DEBUG_BREAK` they trip the allocation tracker a second time mid-recovery. Worse, one overflow path creates a real out-of-bounds write: `sLayerBaseOffsets` is a fixed C array of `kiCollisionLayerPreallocate` (16) entries (`Collision.h:157`), but the `AddLayer` overflow path grows `sLayers` past 16 (`Collision.cpp:51`), after which the `for i < siLayerCount` write at `Collision.cpp:353` (and the reads at `:564`, `:569`) overrun the array — in release builds (no break) that is silent memory corruption.

## Design

### Engine/Source/Frame/Collision.cpp
- Add `ScopedSuppressAllocationTracking` + `// Heap:` comment inside four overflow-growth branches, matching the correctly-suppressed siblings at lines 327–332 / 365–370: `AddLayer`'s `sLayers.resize(siLayerCount * 2)` (line 51), `InsertObjectIntoZones`' `indicesA.resize` (line 114) and `indicesB.resize` (line 125), `SetupZones`' `sLayerPairZones.resize` (line 238 — this one also constructs new `LayerPairZones`, each allocating 128 index vectors) [~15m]
- Fix the `sLayerBaseOffsets` overrun: either make layer-count overflow fatal (assert/cap at `kiCollisionLayerPreallocate` in `AddLayer`) or bound every indexing site against the array size — the write loop in `AllocateResultStorage` (line 353), the pending-result bucketing reads in `Collide` (lines 301, 339), and the span lookups in `HasCollision`/`GetCollisions` (lines 564, 569). The fatal-cap option is structurally simpler (one site instead of four) — pre-staged grill decision [~15m]

### Engine/Source/Frame/AreaDamage.cpp
- Same suppression treatment for `Add`'s `sAreaDamageSources.resize` overflow (line 26) [~5m]

## Critical files
- `Engine/Source/Frame/Collision.cpp`, `Engine/Source/Frame/Collision.h`
- `Engine/Source/Frame/AreaDamage.cpp`

## Acceptance criteria
- Every overflow-growth branch in both files carries the documented suppression + `// Heap:` form.
- Growing past 16 collision layers can no longer write outside `sLayerBaseOffsets` (either impossible by construction or bounded).

## Out of scope
- Raising the preallocate constants (the `DEBUG_BREAK` message already tells the developer to do that)
- The first-use lazy-init resizes (verified fully compliant)
- `CollideLayerPair` decomposition (own plan: `Refactor_CollisionDecomposition.md`, same file — co-schedule)

## Notes
- Overflow-path-only edits — zero behavior change in normal operation, no CRC/determinism/serialization exposure.
- Pre-staged grill decision: fatal-on-overflow vs grow-and-bound for the layer-count overrun.

## Verification Notes (2026-06-10)
- All five unsuppressed overflow branches confirmed: `Collision.cpp:51` (`AddLayer` `sLayers.resize`), `:114`/`:125` (`InsertObjectIntoZones` `indicesA`/`indicesB`), `:238` (`SetupZones` `sLayerPairZones.resize` — each new `LayerPairZones` constructs 8×8 `ZonePair`s × 2 vectors = 128 preallocated index vectors, as claimed), `AreaDamage.cpp:26`. The correctly-suppressed growth siblings at `Collision.cpp:327-332`/`:365-370` and all first-use lazy-init resizes (`:41-44, :182-185, :321-326, :359-364, :392-396`; `AreaDamage.cpp:17-19`) carry suppression + `// Heap:` — "first-use resizes fully compliant" confirmed.
- `sLayerBaseOffsets` overrun chain confirmed: fixed `int64_t [kiCollisionLayerPreallocate]` (= 16) at `Collision.h:157` (defined `Collision.cpp:29`); `AddLayer` growth past 16 at `:47-52`; out-of-bounds write loop at `:351-355`; reads at `:301`, `:339`, `:564`, `:569` (the two `Collide` read sites at :301/:339 were missing from the plan — added).
- "Silent corruption in release" claim validated: `DEBUG_BREAK()` is `if constexpr (kbDebugBreak) { if (IsDebuggerPresent()) __debugbreak(); }` (`Common/ErrorUtils.h:11`), and `kbDebugBreak` is `false` in Profile and Release (`Pch.h:49/66`) — the overflow LOG is `kWarning` (compiled in) but execution continues into the overrun unconditionally; even Debug without a debugger attached continues.
