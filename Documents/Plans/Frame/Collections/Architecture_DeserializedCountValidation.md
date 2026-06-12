# Architecture: Deserialized Count/Capacity Trust-Boundary Validation

## Context

Source: /external-architecture-review on `Engine/Source/Frame/Collections` (non-recursive). `Collection::Read` (`Collection.h:567-575`) and `OptionalIdToIndex::Read` (`Collection.h:482-499`) deserialize `iCount`, `iCapacity`, and the map size with no validation. These values arrive from trust boundaries — save files, replay streams, and the network full-state path (LZ4 framing is validated upstream in `ClientReceive.cpp`, but the decompressed payload's field values are not). A corrupt or malicious stream with `iCount > iCapacity` makes `MultiRead` (`Collection.h:680-683`) write past the buffer allocated for `iCapacity`; a huge `iCapacity` drives an unbounded `MakeAligned` allocation (`CollectionMemory.h:134`). The repo directive requires validation of network input and file reads (root `CLAUDE.md` §Error handling at trust boundaries). Today only `CalculateBufferSize`'s `ASSERT(iCapacity >= 0)` (`CollectionMemory.h:18`) catches negatives — nothing catches `iCount > iCapacity` or absurd magnitudes.

## Design

### Engine/Source/Frame/Collections/Collection.h
- In `Collection::Read` (lines 567-575): after reading, validate `iCount >= 0`, `iCapacity >= 0`, `iCount <= iCapacity`, and an upper bound on `iCapacity` (bound value is the grill decision — see Notes); on failure, follow the codebase's existing trust-boundary failure convention (the fleet-sync plan has landed: `ParseFleetSyncPayload` in `Projects/BrokenEngineSandbox/Source/Network/PlayerEvents.cpp` validates counts via `engine::BoundedCursor` divide-bounds checks, returns false to skip the malformed payload without partial application, and logs at `kNetwork`/`kWarning` — mirror that reject-and-log shape adapted to the stream context) [~30m]
- In `OptionalIdToIndex::Read` (lines 482-499): validate the deserialized `iSize` is non-negative and plausibly bounded (likely `iSize == iCount` for indexable collections — one map entry per live element; confirm the invariant at execution before asserting equality) [~10m]

## Critical files
- `Engine/Source/Frame/Collections/Collection.h`

## Out of scope
- The `AllocateAndAssign` reuse-guard bug (`Architecture_AllocateAndAssignReuseGuard.md`, same code path — co-schedule)
- Validating member payload bytes themselves (the CRC system owns content integrity)
- Upstream framing validation in `ClientReceive.cpp` (already present)

## Acceptance criteria
- A stream presenting `iCount > iCapacity`, negative values, or an out-of-bounds capacity is rejected through the established failure path instead of corrupting memory or allocating unboundedly.
- Healthy save/replay/network loads are unaffected.

## Notes
- **Invariant exposure**: touches the network/file deserialization path; behavior changes only for malformed input — valid-path bytes and CRC values are untouched, no `kiVersion` bump, no protocol change.
- This is sanctioned trust-boundary validation, not internal defensive coding — the values cross file/network boundaries.
- **Pre-staged grill decisions**: (a) the rejection mechanism (stream failbit vs. exception vs. logged abort — the landed fleet-sync convention is reject-whole-payload + `kWarning` log, no partial application; adapt to the stream context); (b) the `iCapacity` sanity ceiling (suggest deriving from a generous max-entity constant rather than inventing a magic number); (c) whether `iSize == iCount` strictly holds for `idToIndexMap`.

## Verification Notes (2026-06-11)
- No-validation claims verified verbatim: `Collection::Read` (`Collection.h:567-575`) and `OptionalIdToIndex::Read` (:482-499 — note `reserve(iSize)` makes a hostile size an unbounded allocation before any insert) take stream values raw; the only existing guard is `ASSERT(iCapacity >= 0)` at `CalculateBufferSize` (`CollectionMemory.h:18`).
- Upstream LZ4 framing validation confirmed (`ClientReceive.cpp:21-34` — size sanity + `LZ4_decompress_safe` result check); decompressed field values are indeed unvalidated after that point.
- `Documents/Plans/Network/ParseFleetSyncWireCountValidation.md` has since been executed and deleted (same session as this note) — the convention to mirror now lives in the implemented code cited in the Design section.
- `iSize == iCount` plausibility: `AddElement`/`AddIndexableElement*`/`RemoveIndexableElement` keep exactly one map entry per live element on the Interpolate side, so equality should hold for indexable collections; the plan's execution-time confirmation before asserting equality is the correct hedge.
- Trust-boundary classification is sound per root CLAUDE.md (network/file reads are sanctioned validation targets). No corrections needed.
