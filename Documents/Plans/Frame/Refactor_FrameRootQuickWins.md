# Refactor: Frame Root Quick Wins (Nav/Collision/Terrain)

## Context
Source: /external-refactor-clean on Engine/Source (recursive). Simulation-adjacent mechanical cleanup. No finding requires reordering floating-point ops in a sim path; the two items touching sim code (pair-context struct, polygon-range helper) are integer/plumbing-only and flagged for byte-identical arithmetic.

## Design

### Engine/Source/Frame/Collision.{h,cpp}
- Delete the write-only `siResultEntryCount` (`Collision.h:107`, defined `.cpp:71`, single assignment `.cpp:366` — never read; result consumption goes through `sResultSpans`/`sResultEntries`) [~5m]
- Bundle the per-layer-pair-invariant tuple (`rLayerA`, `rLayerB`, `uiLayerA`, `uiLayerB`, `bSweptPair`, `rAlignments`) into a pair-context struct built once in `CollideLayerPair` (:507) — `TestAndRecordPair` takes 10 params (:439), `RecordCollision` 7 (:413). Keep bodies byte-identical (sim hot path) [~30m]
- Delete the redundant whole-vector `memset` after the value-initializing `resize` of `sTestedBGeneration` (:522-523 — appended elements are already zero; pre-existing stamps are stale generations that can never match a future generation, and wraparound re-zeroes explicitly at :545) [~5m]

### Engine/Source/Frame/NavQuery.cpp
- Delete the unreachable start→end LOS re-test in `AStarPath` (:505-508) — the sole caller `NavQueryDirection` enters A* exclusively on the else-branch of the identical test with identical coordinates (:662; snapping at :656-659 precedes the test). Costs a full DDA grid walk per A* invocation at 32 Hz. Keep a comment stating the caller's invariant [~5m]
- Pass `fBaseHeight` into `AStarPath` (:459 re-reads `gBaseHeight.Get()`; sibling `NavMissFallbackDirection` takes it as a param, :538; caller holds it, :594) [~5m]
- Drop `NavMissFallbackDirection`'s `iVertexCount` param (:538) — derivable from the `rNavData.vertices` already passed [~5m]
- Single-source the A* scratch layout — `ComputeAStarMemorySize` (:274-286) and `PartitionAStarMemory` (:288-306) duplicate the 7-array layout (and its "bools last" comment); one partition routine that also returns total size [~15m]

### Nav polygon-range helper
- Add `PolygonRange(offsets, iPoly, iVertexTotal)` to `NavBuildInternal.h` and fold the 11 hand-expanded `iStart`/`iEnd`-with-last-polygon-fallback sites: `NavBuild.cpp:280-282,303-305,328-330,546-548`; `NavCellData.cpp:25-27,42-44,152-155,186-189,314-317`; `NavQuery.cpp:172-174,210-212`. Integer-only; keep arithmetic identical (sim-feeding paths) [~30m]

### Engine/Source/Frame/IslandTerrainResidency.cpp (client-only)
- Extract `ChannelCrcs(rLazyChunk, crcs[4])` — the 4-channel CRC array build is copied at :196-202, :254-260, :408-414, :295-301 [~15m]
- Extract `QualifiesForEviction(rTemplate)` — `AnyEvictionPending` (:228-235) hand-mirrors `EvictTemplate`'s condition (:283-290) with a comment promising they match; enforce in code (same pattern, weaker, for `AnyRestorationPending`) [~15m]

### Engine/Source/Frame/TimeStep.h
- Delete the zero-call-site `GetTimeMultiplier()` (:28) — consumers read `miTimeMultiply`/`miTimeDivide` directly per style rule 49 [~5m]

## Critical files
- `Engine/Source/Frame/Collision.{h,cpp}`, `NavQuery.cpp`, `NavBuild.cpp`, `NavCellData.cpp`, `NavBuildInternal.h`, `IslandTerrainResidency.cpp`, `TimeStep.h`

## Out of scope
- Island residency lifecycle (live island-series plans share `IslandTerrainResidency.cpp` — co-schedule or refresh)
- `AlignmentFlags` → `common::Flags` conversion (note-tier; opportunistic only)
- `gBaseHeight` wrapper-in-sim tuning-desync exposure (established repo pattern; not filed)

## Notes
- Invariant exposure: MODERATE — items touch sim-path files but are integer/plumbing/dead-code only; the pair-context and polygon-range changes must keep arithmetic byte-identical (`/fp:strict` paths). Client/server both compile

## Verification Notes
- Verified against source 2026-07-02. All headline claims held: `siResultEntryCount` is write-only (decl `Collision.h:107`, def `.cpp:71`, sole assignment `.cpp:366`); the `AStarPath` start→end LOS test (:505-508) re-runs the exact test its sole caller `NavQueryDirection` failed at :662 with identical (post-snap, :640-659) coordinates; `GetTimeMultiplier()` has zero call sites; `AnyEvictionPending` (:228-235) hand-mirrors `EvictTemplate` (:283-290) with a "Mirrors the EvictionSweep skip logic" comment; all four channel-CRC array builds confirmed (:196-202, :254-260, :295-301, :408-414). A stray-blank-line cosmetic fragment was dropped; polygon-range site count corrected ~8 → 11 (added `NavCellData.cpp:314-317`)
