# Architecture: AllocateAndAssign Reuse Guard Self-Comparison

## Context

Source: /external-architecture-review on `Engine/Source/Frame/Collections` (non-recursive). The buffer-reuse guard in `AllocateAndAssign` (`CollectionMemory.h:118-123`) early-returns when `rStruct.iCapacity >= iCapacity && rStruct.pData != nullptr`, intending to reuse a sufficient existing buffer. But both of its only callers invoke it **after** `rCurrent.Read(rStream)` has already overwritten `rStruct.iCapacity` with the stream value (`AllocateAndRead` at `Collection.h:266-279`, called from `CollectionRead` at `Collection.h:698-704`; and `SharedCollectionRead` at `Collection.h:657-686`). The comparison therefore degenerates to `streamCapacity >= streamCapacity` — always true whenever `pData` is non-null. If a read ever targets a struct whose live buffer was laid out for a *smaller* capacity, the early-return keeps the old member-pointer strides and `MultiRead` (`Collection.h:241-263`) writes through pointers strided for the old capacity — overlapping member regions / heap overrun, with no assert.

Reachability today: network full-state receive and save-load deserialize into fresh frames (`pData` null — safe). The **replay-load path actually exercises the reuse branch**: `ReadGrid` populates frames with allocated buffers, `Game::Reset()` does not clear `mCoordFrames`, then the `DifferenceStreamReader` header frame deserializes into those same non-fresh frames. Benign only because the grid file and replay header are written from the same frames at the same moment so capacities match — an invariant nothing checks; file corruption or any future call-site change breaks it silently.

The justifying comment (`CollectionMemory.h:115-116`, "persistent render interpolates") matches no actual caller — render interpolates go through `Allocate`/`AllocateAndCopy`, not `AllocateAndAssign`.

## Design

### Engine/Source/Frame/Collections/Collection.h
- In `AllocateAndRead` (the helper at lines 266-279) and `SharedCollectionRead` (lines 657-686): capture the struct's pre-`Read` layout state (capacity at which `pData` was laid out) before calling `rCurrent.Read(rStream)`, and make the reuse decision against that captured value instead of the stream-overwritten `iCapacity` [~15m]

### Engine/Source/Frame/Collections/CollectionMemory.h
- Fix `AllocateAndAssign` (lines 117-139) to receive or correctly derive the existing-layout capacity — e.g., an explicit `iExistingLayoutCapacity` parameter, or simply delete the reuse branch and always reallocate on deserialization (the reuse rationale matches no caller; see grill decision in Notes) [~30m]
- Correct or delete the stale "persistent render interpolates" comment at lines 115-116 [~2m]

## Critical files
- `Engine/Source/Frame/Collections/CollectionMemory.h`
- `Engine/Source/Frame/Collections/Collection.h`

## Out of scope
- Validating the stream-supplied `iCount`/`iCapacity` values themselves — own plan (`Architecture_DeserializedCountValidation.md`, same files; co-schedule)
- `Allocate` / `ReallocateIfCapacityChanged` (different contract, per-frame path) and their duplication (`Refactor_AllocationHelperDedup.md`)
- Any change to the serialized format

## Acceptance criteria
- A deserialization into a struct whose live buffer was laid out for a smaller capacity than the stream's reallocates (or asserts) instead of reusing stale strides.
- The healthy replay-load path (matching capacities) still loads without redundant reallocation **or** the reuse branch is deliberately removed — whichever the grill decides.

## Notes
- **Invariant exposure**: touches the deserialization path used by save-load, replay header read, and client full-state receive — cross-frame state, so Risks 3 by blast radius even though the edit is mechanical. No CRC value changes (allocation-only), no `kiVersion` bump (stream format unchanged), no client/server guard changes. Allocation sites stay inside the existing `ScopedSuppressAllocationTracking` scopes.
- **Pre-staged grill decision**: (a) preserve the reuse optimization by passing the pre-`Read` layout capacity, or (b) delete the reuse branch entirely and always reallocate on read (simpler — KISS; reads are infrequent: saves, replays, full-state joins). Recommend (b) unless a hot caller is found at execution.

## Verification Notes (2026-06-11)
- Exhaustive repo grep confirms exactly two `AllocateAndAssign` callers: `AllocateAndRead` (`Collection.h:271`, reached from `CollectionRead` :698-704 after `rCurrent.Read` at :701) and `SharedCollectionRead` (`Collection.h:665`, after `rCurrent.Read` at :660). Both pass the stream-overwritten `rStruct.iCapacity` as `iCapacity`, so the guard at `CollectionMemory.h:120-123` degenerates to `pData != nullptr` exactly as claimed.
- Replay-load reachability verified end-to-end: `GameSaveLoad.cpp` `ReadGrid` (:401-448, clears then repopulates `mCoordFrames` at :425) → replay path :210-233 → `Game::Reset()` (`Game.cpp:422-461`) touches `mActiveCoords` but never `mCoordFrames` → `DifferenceStreamReader` ctor deserializes the header start frame into the existing `pCurrent` via `headerStream >> rSavedStart` (`DifferenceStream.h:185`). Non-fresh-buffer reuse branch is genuinely exercised there.
- Network full-state confirmed fresh: `ClientReceive.cpp:159` `DecompressAndReadFrame` allocates a new `game::Frame` per packet (`pData` null — safe), as the plan states.
- Stale comment confirmed at `CollectionMemory.h:115-116`; per-frame/render copies route through leaf `AllocateAndCopy` → `Allocate` (e.g. `Pushers.cpp:19`, `FrameBase.cpp:165-169`), never `AllocateAndAssign`.
- No corrections needed; all line cites exact.
