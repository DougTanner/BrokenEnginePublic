# Architecture: Determinism Invariant Guards

## Context

Source: /external-architecture-review on `Engine/Source/Frame` (non-recursive). Two load-bearing determinism invariants in this directory are documented but enforced by nothing — each is cheap to turn into a fail-fast debug check, converting a silent replay/cross-build divergence into an immediate break at the violation site.

## Design

### Engine/Source/Frame/NavBuild.cpp
- In `ChainEdgesIntoPolygons` (lines 195–267): the edge-chaining walk does `equal_range` on an `unordered_multimap` (lines 231–257) and stays deterministic only because each cell-edge key has **at most two** entries — so the unused candidate is unique regardless of bucket order. Documented at lines 197–200, asserted nowhere. Add a debug `ASSERT` that no key accumulates more than two entries (cheapest at insertion time, lines 208–212: assert `vertexToEdge.count(key) <= 2` after each insert, or count per key in a debug-only pass). Do **not** assert *exactly* two — boundary cell-edges (contour touching the heightmap border) legitimately have one entry; the chain simply fails to close and is discarded (line 261). The guard protects against future `ExtractContourEdges` edits (e.g., saddle-case changes emitting duplicate keys) — a key with >2 entries would make polygon topology hash-bucket-order-dependent (divergent `NavData` between builds) [~15m]

### Engine/Source/Frame/IslandTerrain.cpp
- `GlobalElevation` (lines 207–268, libm `std::cos`/`std::sin` at 240–241) and `GlobalNormal` (line 834, finite-difference over `GlobalElevation`) walk `mCoordFrames` — the Frame Purity Constraint ("MUST NOT be called from Frame-tick code", `IslandTerrain.h:135-138`) is comment-only. Add a debug assert that the calling thread is not inside a frame tick: a `thread_local` in-tick flag set/cleared via RAII **at the top of `RunFrameTick` itself** (`FrameTick.cpp:12`), so it is owned by whichever thread runs the tick — the per-coord Dispatch workers in normal play and the reconcile thread during client replay (both call `RunFrameTick`). Render-path callers on other threads are unaffected (thread_local). A future sim-path call then fails fast instead of silently desyncing across CPUs [~30m]

## Critical files
- `Engine/Source/Frame/NavBuild.cpp`
- `Engine/Source/Frame/IslandTerrain.cpp` (+ the `RunFrameTick` entry that sets/clears the in-tick flag, `Projects/BrokenEngineSandbox/Source/Frame/FrameTick.cpp`)

## Out of scope
- Making `ChainEdgesIntoPolygons` *handle* >2 entries per key (assert-only; a real fix is a separate plan if the assert ever fires)
- Replacing the `unordered_multimap` with an ordered container (perf/behavior change, not needed if the invariant holds)
- The XOR CRC mixing weakness (own plan: `Architecture_CrcMixingStrength.md`)

## Notes
- Both items are debug-assert-only — zero release-build behavior change, no CRC values change, no serialization/`kiNavDataVersion` exposure.
- Pre-staged grill decision: mechanism for the in-tick flag (`gpThreadLocal` member vs a file-static `thread_local` next to `gpIslandTerrain`).
- `NavBuild.cpp` is also touched by `Frame/NavBuildSplit.md` (the split keeps `ChainEdgesIntoPolygons` in the contour domain) — co-schedule or land after the split to avoid stale line numbers.

## Verification Notes (2026-06-10)
- Verified the multimap walk (`NavBuild.cpp:231-257`), the documenting comment (`:197-200`), and the closed-chain check (`:261`). Cites refreshed (function is 195–267, not 206–256).
- **Corrected the invariant**: the original item asserted *exactly two* entries per key and claimed "a degenerate heightmap" could produce more. Both wrong: (a) cell-edge keys are purely integer-derived (`EncodeEdgeKey(orient,row,col)`, `:30-33`); each marching-squares cell emits each of its four cell-edge keys at most once (verified across the case table including saddle cases 5/10, which use four distinct keys), and at most two cells border any cell-edge — so >2 entries is impossible from heightmap *data*, only from a future *code* change; (b) boundary cell-edges have one adjacent cell, so exactly-two would fire spuriously if a contour ever reaches the heightmap border. Reworded to an at-most-two guard justified as protection against future `ExtractContourEdges` edits.
- Verified `GlobalElevation` walks `mCoordFrames` (`:223`) with libm trig (`:240-241`); `GlobalNormal` (`:834`) inherits both. The purity comment is at `IslandTerrain.h:135-138`. Cites refreshed (the old 224–241 range straddled neither function).
- Verified `RunFrameTick` (`FrameTick.cpp:12`) is the per-coord entry executed on Dispatch worker threads (and by client reconcile replay), so an RAII flag inside it is set on the correct threads; wording adjusted from "around RunFrameTick" (ambiguous — at the dispatch call site it would tag the main thread instead).
