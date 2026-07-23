<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Make Collection Difference Logging Count-Safe

## Context

The replay failure found during server pause/reset verification exposed a second, independent baseline defect. The desktop crash report `Broken-Engine-Sandbox-Server-Crash-Report.txt` records a saved/current `BlastersInterpolate` count mismatch (`1` versus `0`) at tick `39222`, followed by access violation `0xC0000005`. Its stack reaches `DifferenceStreamReader::ValidateChecksum` (`Engine/Source/File/DifferenceStream.h:382`), `Frame::LogDifferences`, and `BlastersInterpolate::LogDifferences` (`Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp:291-301`).

The root cause is deterministic: `Collection::LogDifferences` reports unequal `iCount`/`iCapacity` but returns control to each concrete collection, whose member loop uses the saved collection's full `iCount` and indexes the other collection at the same row. With saved count `1` and current capacity `0`, the first `rOther.puiTypeIndices[0]` dereference is out of bounds. Analogous two-sided loops exist across engine and game collections, so fixing only Blasters would leave the crash class live. The user asked the pause-semantics session to determine whether the crash belonged in that change or a bugfix plan; the main session adjudicated this independent diagnostic root cause as a deferred follow-up under the minimum-sufficient scope boundary.

## Design

1. Establish one count-safe collection-difference contract: compare collection metadata first, then compare member values only for the common valid row range `min(iCount, rOther.iCount)`. The metadata count mismatch remains the authoritative diagnostic for missing/extra rows.
2. Apply the contract to every collection `LogDifferences` implementation that indexes both collections, including engine collections and the game Blaster, missile, player, spaceship, and target collections. Prefer one shared helper for the common row count if it removes repeated boundary logic without changing collection layout or serialization.
3. Preserve detailed field-by-field logging for every common row even when counts differ. Do not return immediately after the metadata mismatch, suppress CRC errors, fabricate default rows, or read unused capacity beyond either live count.
4. Keep the change diagnostic-only: no simulation mutation, collection count/capacity repair, save/replay compatibility behavior, or CRC composition change.

## Critical files

- `Engine/Source/Frame/Collections/Collection.h` — shared `Collection::LogDifferences` metadata comparison and possible common-count helper.
- `Engine/Source/File/DifferenceStream.h` — `DifferenceStreamReader::ValidateChecksum` caller and mismatch-reporting contract; inspect, but avoid changing checksum behavior.
- `Engine/Source/Frame/Collections/Explosions/Explosions.cpp` and `Pushers/Pushers.cpp` — engine collection member comparisons.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.cpp` — proven crashing loop.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/Missiles.cpp`, `Players/Players.cpp`, `Spaceships/Spaceships.cpp`, and `Targets/Targets.cpp` — analogous game collection loops.
- `Projects/BrokenEngineSandbox/Source/Frame/Frame.cpp` and `Engine/Source/Frame/FrameUtils.h` — aggregate difference walk; verify call order and propagation only.

## Out of scope

- Fixing the underlying replay transfer omission or any live client/server Blaster divergence.
- Recovering or reconciling unequal collections, changing CRC coverage, or altering save/network deserialization.
- Adding per-element diagnostics for rows that exist on only one side; the already-logged count mismatch identifies them without unsafe reads.

## Acceptance criteria

- A known saved/current collection mismatch with counts `1` and `0` logs the collection count/capacity difference and final CRC difference without an exception or out-of-bounds access.
- A mismatch with unequal nonzero counts still logs every differing field in the common rows and does not access rows at or beyond `min(iCount, rOther.iCount)`.
- Static coverage confirms every engine/game collection `LogDifferences` loop that indexes both sides follows the common-row bound; the fix is not Blaster-specific.
- Equal-count replay mismatch diagnostics retain their existing field names, row indices, and CRC output.
- Debug client and server builds pass; the known replay transfer defect may still produce a deterministic CRC mismatch, but mismatch reporting itself must complete without crashing.

## Notes

- Tier 3 by workflow trigger because the diagnostic contract spans engine and game collection implementations; runtime risk remains low because it changes only mismatch logging.
- Scoring: Effort 2, Impact 4, Risks 1, Score -1. This is a narrow cross-codebase safety fix that removes a repeatable crash class without touching deterministic state.
- Collection memory layout, `kiVersion`, persisted replay/save bytes, wire protocol, shared CRC values, and simulation order must remain unchanged.
