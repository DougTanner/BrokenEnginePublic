# Architecture: Collection-Internal CRC Mixing (XOR → Ordered Fold)

## Context

Source: /external-architecture-review on `Engine/Source/Frame/Collections` (non-recursive). This is the collection-internal pass that `Frame/Architecture_CrcMixingStrength.md` explicitly carved out of scope ("collection-internal mixing is a separate, larger pass if ever wanted"). The same XOR self-cancellation weakness exists inside the collection templates:

- `Collection::Crc` (`Collection.h:577-587`) combines `Crc(iCount) ^ Crc(iCapacity)`: whenever `iCount == iCapacity` (common — collections fill to capacity), the two terms are identical and cancel to 0, so the metadata contribution is the same for *any* full collection.
- `OptionalIdToIndex::Crc` (`Collection.h:501-523`) XORs per-entry key and value hashes: swapping the index values of two map entries is invisible. The keys are already sorted into the workbuffer (lines 507-514), so order-independence is not required — an ordered fold costs nothing.
- `MultiCrc` (`Collection.h:186-213`) XORs per-member-array CRCs: identical corruption mirrored in two member arrays cancels. Tuple order is compile-time fixed, so an ordered fold is deterministic across builds.
- `CollectionCrc` (`Collection.h:600-606`) continues the XOR pattern at its combine site.

Impact is detection sensitivity only — realistic single-site desyncs are caught by the member-array byte folds — but the `iCount == iCapacity` metadata cancellation is systematic, not exotic.

## Design

### Engine/Source/Frame/Collections/Collection.h
- `Collection::Crc` (lines 577-587): replace the XOR combine of `iCount`/`iCapacity`/map terms with an ordered fold (e.g., `crc = crc * common::kCrcMultiplier + term`, matching the `common::Crc` byte-fold convention used by `Architecture_CrcMixingStrength.md`) [~10m]
- `OptionalIdToIndex::Crc` (lines 501-523): fold the sorted key/value pairs in order instead of XORing [~10m]
- `MultiCrc` (lines 186-213): ordered fold across member arrays (and across `T* pp[N]` array elements) [~10m]
- `CollectionCrc` (lines 600-606): same conversion at the combine site [~10m]

## Critical files
- `Engine/Source/Frame/Collections/Collection.h`

## Out of scope
- The frame-level XOR sites (`Alignments.cpp`, `FrameBase.cpp`, game `Frame.cpp`) — owned by `Frame/Architecture_CrcMixingStrength.md`; this plan extends the same convention downward
- The two-target helper `common::Crc(value, rCrc, rSharedCrc)` (formerly `Crc.h:123-130`) — **deleted** by `Refactor_DeadElementCrcChain` (dead, zero callers); no longer a fold target
- The hash function itself (`common::Crc` internals)

## Notes
- **Determinism/CRC exposure: shared CRC values change.** Client and server must be rebuilt together (always true). Replay `.checksums` files persist per-tick `Frame::Crc()` values — old replays will play but log per-tick `kError` CRC mismatches, so they must be re-recorded. Same accepted class as `Architecture_CrcMixingStrength.md`; no `kiVersion` bump (serialized state layout unchanged), no `.pack` impact.
- **Dependency**: land with or after `Frame/Architecture_CrcMixingStrength.md` so the fold convention is established once and replays are re-recorded once, not twice.

## Verification Notes (2026-06-11)
- All four XOR sites verified at the cited lines: `Collection::Crc` (:577-587), `OptionalIdToIndex::Crc` (:501-523, keys sorted into workbuffer at :507-514 so order-independence is genuinely unneeded), `MultiCrc` (:186-213), `CollectionCrc` (:600-606). (`MultiElementCrc` was a fifth site, since **deleted** by `Refactor_DeadElementCrcChain`.)
- The `iCount == iCapacity` cancellation is real and systematic: both go through the same `common::Crc(int64_t)`, so equal values produce identical hashes that XOR to zero; capacity growth `2c+1` makes exact-fill states common (capacity 1/count 1, etc.).
- Dependency plan `Frame/Architecture_CrcMixingStrength.md` exists, survived its own verification (2026-06-10), and its Out-of-scope carve-out covers collection-internal mixing verbatim ("a separate, larger pass if ever wanted") — no duplication, clean handoff.
- `common::kCrcMultiplier` confirmed (`Crc.h:50`); the two-target helper formerly carved out in Out-of-scope has since been **deleted** by `Refactor_DeadElementCrcChain` (dead, zero callers) — no longer a fold target.
- Replay `.checksums` impact statement matches the sibling plan's verified mechanism (per-tick `kError` log, not load failure). No corrections needed.
