# Collection::Read Deserialization Index & Capacity-Stride Hardening

## Context

Surfaced (and extension-review-gated) during the `Save/SaveLoadTrustBoundaryHardening` session (2026-07) and deferred here by user decision — it exceeds that plan's server-only/file-only/no-CRC envelope. Two linked gaps in the SHARED engine collection deserialization framework `Engine/Source/Frame/Collections/Collection.h`, which is reached from BOTH the server save/load path AND the client live-network full-state receive (`OptionalIdToIndex::Read` ← `Collection::Read` ← `SharedCollectionRead` ← `FrameInterpolateBase`/`FramePostRenderBase::ServerRead` ← `ClientReceive.cpp:41`), and whose `OptionalIdToIndex::Crc` participates in the shared CRC (`SharedCollectionCrc`):

1. **Unvalidated SOA index in `OptionalIdToIndex<T>::Read`** (`Collection.h` ~:226-245): reads a raw `int64_t` index from the stream and stores it via `insert_or_assign` with no `[0, iCount)` range check (values are arbitrary — negative, `>= iCount`, or duplicated). `Collection::Read` (~:324-336) validates the entry *count* and that `idToIndexMap.size() == iCount`, but never that each stored index value falls in `[0, iCount)`. Consumers subscript SOA arrays through `IdToIndex(id)` (`Collection.h:205-208`, e.g. `Spaceships.cpp` `puiTargets[iTargetIndex]`), so a corrupt save — or a hostile/corrupt network full-state — with an in-count map holding an out-of-range value yields an out-of-bounds heap read (server on load, client on network receive). Worse, the hostile index also reaches an OOB **write**: `RemoveIndexableElement` (`Collection.h:584`) does `iIndex = idToIndexMap.at(id)` then `SwapElement(rInterpolate, iIndex, …)`, which writes `memberPtrRefs[iIndex] = memberPtrRefs[iCount-1]` at the attacker-controlled index. A `common::CorruptStreamException` thrown at read degrades safely on both paths: the client drops the packet before slot mutation (`Client.cpp:235-242`), the server load falls back clean (`ReadGrid` catch). (Only `PlayersInterpolate` / `TargetsInterpolate` are `kIdToIndex` today, and both remove via `RemoveIndexableElement` — so the write path is live.)

2. **Item-7 byte ceiling is inert at this call site** (`Collection.h` ~:331): `Collection::Read` calls `ValidateDeserializedCountCapacity(iCount, iCapacity, /*iElementBytes=*/1, …)` — the comment says "member stride unknown here." The 256 MiB capacity-bytes ceiling added by `SaveLoadTrustBoundaryHardening` (`Common/Serialization.h` `kiMaxDeserializedBytes`) therefore reduces to `iCapacity > 256M/1`, which never fires before the existing 16M-element cap; a hostile stream reaches a multi-GB SOA reserve (`iCapacity × Σ(member strides)`) despite the ceiling. Threading the real per-element SOA stride would make the capacity-bytes bound effective (guards a large-stride × up-to-16M-element reserve — the multi-GB reservation the original item targeted). Note the metadata `Collection::Read` layer does NOT hold the member tuple, but its callers `CollectionRead` / `SharedCollectionRead` DO (`rCurrent.Members()`), so the real per-element byte total (sum of member-row `sizeof`s) can be computed there and re-validated before `AllocateAndAssign` — thread it down, or add the re-validation at the caller layer.

## Design

1. Validate every stored index in `[0, iCount)`, throwing `common::CorruptStreamException` on violation. Grill/design the shape:
   - **Option A** — thread the valid bound (`iCount`) into `OptionalIdToIndex<T>::Read` via a signature change and validate each value per-read.
   - **Option B** — a second validation pass in `Collection::Read` after the map is built (where `iCount`/`iCapacity` are already in scope), walking the stored values against `iCount`. Less invasive to the shared template signature.
2. Thread the real per-element SOA stride into the `ValidateDeserializedCountCapacity` call at `Collection::Read` so the byte ceiling becomes effective. The collection knows its member layout at that layer — derive the per-element byte footprint from the SOA member set (e.g. sum of member-row `sizeof`s, or the largest member stride) rather than the placeholder `1`.

## Critical files

- `Engine/Source/Frame/Collections/Collection.h` — `OptionalIdToIndex<T>::Read` / `Read`, `IdToIndex`, `Collection::Read`, the `ValidateDeserializedCountCapacity` call
- (read-only reference) `Engine/Source/Frame/FrameBase.cpp` — `FrameInterpolateBase`/`FramePostRenderBase::ServerRead` / `Crcs`; `Engine/Source/Network/Client/ClientReceive.cpp` — the full-state receive entry

## Invariant exposure

- **Shared engine collection deserialization — server save/load AND client live-network full-state receive; the map participates in the CRC.** Behavior-preserving for known-good input (byte-identical reconstruction); the change only converts OOB-read/torn outcomes on corrupt/hostile input into uniform `CorruptStreamException` rejection (caught on both paths). No wire / `kiVersion` / `.pack` layout change.
- Because it sits on the deterministic reconstruction path, it needs a replay/CRC soak AND a network full-state soak.

## Out of scope

- The server-only file-side gaps already closed by `Save/SaveLoadTrustBoundaryHardening` (FrameInput count, `iFlagshipIndex`, `iNextGlobalId`, client-coord, etc.).
- The network StatusChange batch codec — `Network/StatusChangeCodecHardening.md`.

## Acceptance criteria

- A corrupt save or full-state carrying an in-count index map with an out-of-range value → `CorruptStreamException` rejection (server load falls back clean; client drops the packet), never an OOB SOA read.
- A known-good save / replay / network full-state round-trips byte-identically (CRC unchanged).
- The capacity-bytes ceiling fires for an oversized-stride reserve (the byte bound is no longer inert).

## Notes

- Surfaced + extension-review-gated during the `SaveLoadTrustBoundaryHardening` session; deferred here by user decision (out of that plan's envelope: shared engine/CRC/client-network path, needs a signature/second-pass design choice).
- Grill: Option A (thread the bound) vs Option B (second pass) for the index check.
- Shares `Engine/Source/Frame/Collections/Collection.h` with the Frame-collections series (`Frame/Architecture_CollectionCopyContract.md`, `Frame/Architecture_CollectionHelperDedup.md`, `Frame/Architecture_PhaseHookOptIn.md`, `Frame/CollectionDeserializationHardening.md`, `Frame/Refactor_CollectionHeadersQuickWins.md`) — co-schedule / refresh citations; touches the client full-state receive path shared with the Network full-state plans.
- **Sibling deserialization-hardening plan:** `Frame/CollectionDeserializationHardening.md` owns the memory-safety gaps on the same read path (A1 shrink-reuse under-zero, A3 alloc-failure invariant, A5 paired-count parity); this plan owns SOA-index-value validation and the real-stride capacity-byte ceiling. Item 2 above (thread the real per-element stride) is the same finding that plan cross-references — keep the two in sync; land together.
