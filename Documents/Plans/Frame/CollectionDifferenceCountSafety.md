<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Make Collection Difference Logging Count-Safe

## Context

The replay failure found during server pause/reset verification exposed a second, independent baseline defect. The desktop crash report `Broken-Engine-Sandbox-Server-Crash-Report.txt` records a saved/current `BlastersInterpolate` count mismatch (`1` versus `0`) at tick `39222`, followed by access violation `0xC0000005`. Its stack reaches `DifferenceStreamReader::ValidateChecksum` (`Engine/Source/File/DifferenceStream.h`, the `savedFrame.LogDifferences(rSavedCurrent)` call on checksum mismatch), `Frame::LogDifferences`, and `BlastersInterpolate::LogDifferences` (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp`).

The root cause is deterministic and confirmed in current code: `Collection::LogDifferences` (`Engine/Source/Frame/Collections/Collection.h`) logs unequal `iCount`/`iCapacity` but returns control to each concrete collection, whose member loop runs `for (int64_t i = 0; i < iCount; ++i)` over this side's full count while indexing `rOther`'s member arrays at the same row. With saved count `1` and current count `0`, the first `rOther.puiTypeIndices[0]` dereference is out of bounds. The identical two-sided loop shape exists in every engine and game collection `LogDifferences` listed below, so fixing only Blasters would leave the crash class live.

## Design

1. Add one count-safe common-row helper to the collection base in `Engine/Source/Frame/Collections/Collection.h`, adjacent to `Collection::LogDifferences`: an inline `const` member returning `std::min(iCount, rOther.iCount)` for a `const Collection&` parameter (exact name is the implementer's choice; suggested `CommonRowCount`). It changes no data members, no layout, no serialization, and no CRC.
2. In every concrete `LogDifferences` implementation listed under In scope, replace the row-loop bound `i < iCount` with the helper's common-row count. The existing `Collection::LogDifferences` metadata comparison (`iCount`, `iCapacity`, and `idToIndexMap.size` where present) remains the authoritative diagnostic for missing/extra rows and is not modified.
3. Preserve detailed field-by-field logging for every common row even when counts differ. Do not return early after the metadata mismatch, suppress CRC errors, fabricate default rows, or read past either side's live count. In `ExplosionsInterpolate::LogDifferences`, the nested trail loop keeps its existing `j < piTrailCounts[i]` bound (trail slots are fixed member-array slots, not rows); only its enclosing row loop takes the common-row bound.
4. The change is diagnostic-only: no simulation mutation, no collection count/capacity repair, no save/replay compatibility behavior, no CRC composition change.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change below and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named functions/regions plus the mechanical necessities (includes, declarations) the named change requires.

### In scope

- `Engine/Source/Frame/Collections/Collection.h` — add the common-row helper next to the existing `Collection::LogDifferences` member (the region near line 292). No other member of `Collection` changes.
- `Engine/Source/Frame/Collections/Explosions/Explosions.cpp` — `ExplosionsInterpolate::LogDifferences` outer row loop only. `ExplosionsPostRender::LogDifferences` has no row loop and is not modified.
- `Engine/Source/Frame/Collections/Pushers/Pushers.cpp` — row loops in `PushersInterpolate::LogDifferences` and `PushersPostRender::LogDifferences`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp` — row loops in `BlastersInterpolate::LogDifferences` and `BlastersPostRender::LogDifferences` (the proven crashing site).
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp` — row loops in `MissilesInterpolate::LogDifferences` and `MissilesPostRender::LogDifferences`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Players/Players.cpp` — row loops in `PlayersInterpolate::LogDifferences` and `PlayersPostRender::LogDifferences`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/Spaceships.cpp` — row loops in `SpaceshipsInterpolate::LogDifferences` and `SpaceshipsPostRender::LogDifferences`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.cpp` — row loops in `TargetsInterpolate::LogDifferences` and `TargetsPostRender::LogDifferences`.

### Inspect only, no edits

- `Engine/Source/File/DifferenceStream.h` — `DifferenceStreamReader::ValidateChecksum` mismatch-reporting path; confirm call order, change no checksum behavior.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` (`FrameInterpolate::LogDifferences`, `FramePostRender::LogDifferences`, `Frame::LogDifferences`), `Engine/Source/Frame/FrameBase.cpp` (`FrameInterpolateBase::LogDifferences`, `FramePostRenderBase::LogDifferences`), and `Engine/Source/Frame/FrameUtils.h` (`LogDifferencesCollections`) — aggregate difference walk; verify propagation only. These compare frame-level state or dispatch into collections and contain no two-sided row loops.

### Out of scope

- Fixing the underlying replay transfer omission or any live client/server Blaster divergence (tracked separately in `Documents/Plans/Network/BlasterReconciliationDesync.md`).
- Recovering or reconciling unequal collections, changing CRC coverage, or altering save/network deserialization.
- Adding per-element diagnostics for rows that exist on only one side; the already-logged count mismatch identifies them without unsafe reads.
- Any change to `Members()`/`SharedMembers()` tuples, collection layout, `kiVersion`, persisted bytes, or wire protocol.

## Risk tier and invariants

Tier 3 by workflow trigger: the diagnostic contract spans engine and game collection implementations across independently owned subsystems. Runtime risk remains low because only mismatch logging changes. Invariants that must remain unchanged: collection memory layout, `kiVersion`, persisted replay/save bytes, wire protocol, shared CRC values, and simulation order. Equal-count paths must produce byte-identical diagnostics to today.

## Acceptance criteria

- A saved/current collection mismatch with counts `1` and `0` logs the collection count/capacity difference and the final `LogDifferences CRC` line without an exception or out-of-bounds access.
- A mismatch with unequal nonzero counts still logs every differing field in the common rows and accesses no row at or beyond `min(iCount, rOther.iCount)` on either side.
- Static coverage confirms every engine/game collection `LogDifferences` row loop that indexes both sides uses the common-row bound — all thirteen loops listed under In scope; the fix is not Blaster-specific.
- Equal-count replay mismatch diagnostics retain their existing field names, row indices, and CRC output.
- Debug client and server builds pass; the known replay transfer defect may still produce a deterministic CRC mismatch, but mismatch reporting itself must complete without crashing.

## Notes

- Scoring: Effort 2, Impact 4, Risks 1. Narrow cross-codebase safety fix removing a repeatable crash class without touching deterministic state.
