# Refactor: Frame Diagnostic and Dead-Code Removal

## Context

Source: /external-refactor-clean on `Engine/Source/Frame` (non-recursive). Four verified zero-dependency deletions: two finished diagnostics (one of which logs per-cell at `kInfo` in shipping builds and one of which does O(vertices) dead work every cell build), and two dead APIs with zero callers repo-wide.

## Design

### Engine/Source/Frame/IslandChainPlacement.cpp
- Delete the self-marked `kTemp` fill diagnostic in `Generate` (lines 397–414: shoelace loop + `LOG(kTemp, kInfo, ...)`) — comment says "Remove once placement density / cell size are dialed in"; `keLogLevelTemp = kVerbose` (`Pch.h:82`) means it logs on **every cell creation** at `kInfo`, violating the root log-level guidance for recurring messages. `fLandAreaMeters`/`fCellAreaMeters`/`fFillPercent` are consumed only by the LOG. Side benefit: `Generate` drops under the 100-line threshold [~5m]

### Engine/Source/Frame/NavBuild.cpp
- Delete the per-polygon bounds scan in `BuildCellNavData` (lines 603–626): the min/max loop runs unconditionally but feeds only a `kVerbose` LOG that compiles out (`keLogLevelNavData = kWarning`, `Pch.h:84/88`; `Log.h:143-149` is `if constexpr`) — O(total cell vertices) of dead work per coord build, self-marked "for density analysis" [~5m]

### Engine/Source/Frame/TimeStep.h
- Delete `GetAverageDelta()` (line 53) — zero callers repo-wide; when `kbProfilingFrameSpike` is `false` (2 of 3 configs) it returns a never-written value, so it is dead *and* misleading. The `mAverageDelta` member stays (it backs the spike profiler) [~2m]

### Engine/Source/Frame/Alignments.h / Alignments.cpp
- Delete `Alignments::RemoveAlignment` (`Alignments.h:49`, `Alignments.cpp:37-46`) — zero callers repo-wide (only `AddAlignment` is called, once, at `Game.cpp:46`) [~5m]

## Critical files
- `Engine/Source/Frame/IslandChainPlacement.cpp`
- `Engine/Source/Frame/NavBuild.cpp` (post-split: `NavCellData.cpp`)
- `Engine/Source/Frame/TimeStep.h`
- `Engine/Source/Frame/Alignments.h`, `Engine/Source/Frame/Alignments.cpp`

## Out of scope
- Items already owned by `Engine/DeadCodeAndUnusedIncludesSweep.md` (no overlap — verified)
- Any behavior change beyond removing the dead/diagnostic code itself
- The `kbDebugNavCrossingCheck` block (live diagnostic, kept; its extraction is `Refactor_NavFunctionDecomposition.md`)

## Notes
- No CRC/determinism/serialization exposure (LOG and dead-API removals only; the deleted NavBuild loop wrote nothing).
- The `NavBuild.cpp` item edits `BuildCellNavData`, which `Frame/NavBuildSplit.md` relocates — co-schedule or land after the split.
- If alliance changes are on the roadmap, keep `RemoveAlignment` with a note instead — pre-staged grill question.

## Verification Notes (2026-06-10)
- `IslandChainPlacement.cpp:397-414` confirmed: shoelace loop over `placedHullViews` feeding only the `LOG(kTemp, kInfo, ...)` at :414; "Remove once placement density / cell size are dialed in" self-mark at :399. `keLogLevelTemp = kVerbose` (`Pch.h:82`) means the kInfo call passes the `Log.h:143-149` `if constexpr` filter in **all three** configs — per-cell-generation logging in shipping builds, as claimed. `fLandAreaMeters`/`fCellAreaMeters`/`fFillPercent` have no other consumers.
- `NavBuild.cpp:603-626` confirmed: min/max bounds loop feeds only the `LOG(kNavData, kVerbose, ...)` at :625, which compiles out (`keLogLevelNavData = keLogLevelDefault = kWarning`, `Pch.h:84/88`); the loop's `.at()` calls prevent the optimizer from deleting it — genuine O(cell vertices) dead work per coord build.
- `GetAverageDelta()` (`TimeStep.h:53`): repo-wide grep finds zero callers. `mAverageDelta` is written only inside `if constexpr (kbProfilingFrameSpike)` (`TimeStep.cpp:14-31`), which is `true` only in Profile (`Pch.h:39/56/73`) — "never-written in 2 of 3 configs" confirmed. The member's retention rationale (spike profiler reads at `TimeStep.cpp:18/20`) confirmed.
- `Alignments::RemoveAlignment` (`Alignments.h:49`, `Alignments.cpp:37-46`): repo-wide grep finds zero callers; `AddAlignment` called exactly once (`Game.cpp:46`). All cites exact.
- Overlap with `Engine/DeadCodeAndUnusedIncludesSweep.md` re-checked: none of its 11 items touch these four removals.
