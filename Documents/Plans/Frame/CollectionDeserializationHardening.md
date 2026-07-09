# Collection Deserialization Memory-Safety Hardening

## Context

Adversarial audit (2026-07) of the shared engine SOA collection framework `Engine/Source/Frame/Collections/{Collection.h, CollectionMemory.h}` — reached from BOTH the server save/load path AND the client live-network full-state receive (`SharedCollectionRead` ← `FrameInterpolateBase`/`FramePostRenderBase::ServerRead` ← `ClientReceive.cpp`). Three memory-safety gaps distinct from the index/stride validation in `Frame/CollectionReadIndexHardening.md` (which owns SOA-index-value and capacity-byte-ceiling validation — co-schedule, same files).

**A1 [HIGH] — `SharedCollectionRead` under-zeroes after consecutive shrinking reuse loads.** `Collection.h` `SharedCollectionRead` (~:435-443) sizes its zero-fill `memset` from `std::max(iExistingLayoutCapacity, rCurrent.iCapacity)`, but `iExistingLayoutCapacity` is only the *last-recorded logical* capacity (`rCurrent.iCapacity` before `Read`), NOT the buffer's true physical layout stride. Scenario: allocate at cap 100 → shared read at 70 (`AllocateAndAssign` reuses the cap-100 buffer, `iCapacity`←70; `max(100,70)=100`, still fully zeroed) → shared read at 60 (`iExistingLayoutCapacity` is now the stale logical 70, not the physical 100; reuse again; `max(70,60)=70`). The buffer is still physically strided for 100, so the `memset` covers only a cap-70 extent — the later member rows (laid out at stride-100 offsets) are never zeroed. Client-only fields (which `SharedCollectionRead` zero-fills precisely so they default to invalid-ID/null) then hold stale garbage for live elements. Reachable via repeated client reconnects with shrinking capacities. Not a CRC/desync (the affected fields are client-only, out of the CRC) — but real client-side corruption (stale IDs / dangling visual references). **Fixable independently and highest priority.**

**A3 [MED] — allocation failure leaves the "null pData ⇒ zero capacity" invariant broken.** `common::MakeAligned` returns null on `_aligned_malloc` failure without throwing. `AllocateAndAssign` (`CollectionMemory.h:132-133`) sets `rStruct.iCapacity = iCapacity` BEFORE `rStruct.pData = MakeAligned(...)`, so a failed allocation leaves a nonzero capacity with null `pData` — violating the invariant `AllocateCore` explicitly relies on (`CollectionMemory.h:203-204`: "a null pData always implies zero capacity"). `SharedCollectionRead` then `memset`s through the null pointer → AV. Reachable: deserialization can legitimately request `kiMaxDeserializedCapacity` (16M) × a real per-element stride, a multi-GB reservation that fails. Should surface as a clean `CorruptStreamException` (caught on both the save-load and network paths), not an access violation.

**A5 [MED] — paired Interpolate/PostRender count/capacity parity never validated at the deserialization boundary.** `Collection.h` `GrowPairedCollections` (~:516-520) asserts `rInterpolate.iCount == rPostRender.iCount`, but that ASSERT fires only at spawn-time growth — a corrupt stream carrying a smaller PostRender capacity than its paired Interpolate is accepted at read, and a subsequent spawn writes OOB into the under-sized PostRender arrays before the growth ASSERT is ever reached. The parity is maintained by construction on every benign path (see R2 below), so a boundary check is a no-op on valid input.

## Design

### A1 — track true physical layout capacity (recommended)
Give the collection a transient, non-serialized physical-layout-capacity (element count the buffer is currently strided for). `AllocateAndAssign` / `GrowCapacityWithCopy` / `AllocateCore` set it to the actual `iCapacity` at every real `MakeAligned`, and leave it unchanged on the reuse early-return. `SharedCollectionRead` sizes its `memset` from that value (and the `AllocateAndAssign` reuse guard can use it too). It must stay out of `Write`/`Read`/`Crc`/`LogDifferences` — derived state, never serialized. Grill: new member on `Collection` vs. storing the allocated buffer byte size directly (either recovers the true stride after a shrink; a byte-size field also lets `memset` skip the per-member re-sum).

### A3 — clean-fail on allocation failure
Primary fix at `AllocateAndAssign`: after `MakeAligned`, if `pData == nullptr` throw `common::CorruptStreamException` (the type both deserialization catchers already handle), and order the writes so a failure never leaves nonzero `iCapacity` with null `pData` (set `pData` first, or reset `iCapacity` on the throw path). Note the same MakeAligned-returns-null hazard exists at `AllocateCore` (:213) and `GrowCapacityWithCopy` (:258); those are not reachable with hostile sizes (previous-frame / 2×-growth capacities), so treat them as lower-priority robustness — decide at grill whether to fold a shared null-guard or make `common::MakeAligned` itself throw (broader, cross-cutting — coordinate with the `Common/CommonPrimitivesHardening.md` MakeAligned item).

### A5 — validate parity at the read boundary
Add a paired-parity check where the paired collections are read (the `FrameBase.cpp` `Read`/`ServerRead` walk, before any Spawn/Grow): reject a stream whose paired Interpolate/PostRender `iCount` (and capacity relationship) disagree with `common::CorruptStreamException`, rather than relying on the too-late `GrowPairedCollections` spawn-time ASSERT.

## Critical files

- `Engine/Source/Frame/Collections/Collection.h` — `SharedCollectionRead` (A1 memset extent), `Collection` (A1 physical-capacity member if chosen), `GrowPairedCollections` (A5 reference)
- `Engine/Source/Frame/Collections/CollectionMemory.h` — `AllocateAndAssign` (A1 physical-capacity write + A3 null-guard/ordering), `AllocateCore`/`GrowCapacityWithCopy` (A1 write, A3 lower-priority)
- `Engine/Source/Frame/FrameBase.cpp` — the paired-collection `Read`/`ServerRead` walk (A5 parity check)
- (read-only reference) `Engine/Source/Network/Client/ClientReceive.cpp` — the full-state receive entry

## Out of scope

- SOA-index-value validation and the real-stride capacity-byte ceiling — `Frame/CollectionReadIndexHardening.md`.
- The copy/zero-init contract, member-visitor dedup, and phase-hook redesign (`Frame/Architecture_CollectionCopyContract.md`, `Frame/Refactor_CollectionMemberVisitor.md`, `Frame/Architecture_PhaseHookOptIn.md`) — same files, separate sessions.
- Any wire / `kiVersion` / `.pack` / CRC-format change.

## Acceptance criteria

- After a shrink-then-shrink reuse sequence, every member row of a live element is fully zero-initialized before the shared read (no stale client-only garbage) — A1.
- A deserialization that requests an unallocatable buffer throws `CorruptStreamException` (caught: server load falls back clean, client drops the packet), never an AV; no `iCapacity`-nonzero-with-null-`pData` state survives — A3.
- A corrupt stream with mismatched paired counts/capacities is rejected at the read boundary, before any OOB spawn write — A5.
- A known-good save / replay / network full-state round-trips byte-identically (CRC unchanged); all three changes are no-ops on valid input.

## Invariant exposure

- **Shared engine deserialization framework — server save/load AND client live-network full-state receive.** A1's affected fields are client-only (out of the CRC), so A1 is client-side corruption, not desync. A3/A5 are no-ops on any validly-produced stream — they only convert AV / OOB-write outcomes on corrupt/hostile input into uniform `CorruptStreamException` rejection. No wire / `kiVersion` / `.pack` / CRC-format change. Because it sits on the deterministic reconstruction path, verify with a replay/CRC soak AND a network full-state soak.

## Notes

- Grill decisions: A1 physical-capacity member vs. stored buffer byte size; A3 local null-guard vs. `common::MakeAligned` throwing at source.
- **R2 resolved (benign parity):** `AddElement`/`GrowPairedCollections` grow both paired collections to one shared `iNewCapacity` in lockstep, and each is written from matched in-memory capacities, so no benign save/replay/broadcast path diverges paired counts/capacities — only a corrupt/hostile stream does (A5). The parity is maintained by construction, asserted only at spawn-time growth, never validated at read — which is the gap A5 closes.
- Shares `Collection.h`/`CollectionMemory.h`/`FrameBase.cpp` with the Frame-collections series — co-schedule / refresh citations per the Order.md Frame-collections Dependencies + File Group entries.
