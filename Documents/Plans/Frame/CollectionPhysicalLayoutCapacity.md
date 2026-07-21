# Collection Physical Layout Capacity Retention

## Context

A post-reconciliation session audit found one unresolved shrink-reuse defect in the shared SOA deserialization framework after the other collection deserialization hardening landed. `Collection<T>` currently stores only `iCount`, serialized/logical `iCapacity`, and `pData` (`Engine/Source/Frame/Collections/Collection.h`, `Collection`). It has no transient field recording the element capacity used to stride the installed buffer.

`SharedCollectionRead` captures `rCurrent.iCapacity` as `iExistingLayoutCapacity` before `Read` overwrites it, and `AllocateAndAssign` reuses `pData` when that captured value is large enough. This is sufficient for one shrink but loses the true layout extent afterward. Starting with a buffer physically strided for 100 rows, reads at logical capacities 70 then 60 reuse the same buffer; after the first read `iCapacity` records 70, so the second read treats 70 as the physical extent. Its zero-fill is sized for 70 even though member pointers remain laid out at stride 100, leaving part of the client-only storage stale. The comments at `Collection.h:387-394` call the captured value physical-layout capacity, but the stored value is only the most recent logical capacity.

The completed deserialization work already owns allocation-failure cleanup and paired Interpolate/PostRender parity. This residual is specifically the missing persistent physical-layout extent. It predates the collection copy-contract session; the tuple-copy folds are compatible with the corrected allocation metadata and require no redesign.

## Design

- Add one transient `iPhysicalLayoutCapacity` field to `Collection<T>` as the authoritative element capacity used to lay out the currently installed `pData` buffer.
- Maintain the field at every buffer ownership transition: set it only after successful allocation in `AllocateAndAssign`, `Allocate`, and `GrowCapacityWithCopy`; preserve it on reuse; and clear it with `pData` in `ResetDataToNull`. Positive-capacity empty-member layouts must retain the same invariant.
- Make full and shared collection deserialization use `iPhysicalLayoutCapacity` for allocation-reuse decisions. Size `SharedCollectionRead`'s complete-buffer zero-fill from that physical extent, never from a prior logical `iCapacity` snapshot.
- Keep `iCapacity` as the stream-visible logical capacity and CRC input. `iPhysicalLayoutCapacity` is derived storage state: exclude it from `Write`, `Read`, `Crc`, `LogDifferences`, member tuples, serialization, and versioning.
- Add one debug-input-only `collection_layout_capacity_fixture` command to the shared dispatch in `AgentCommands.cpp`. Use the real `BlastersPostRender` collection and invoke the production deserialization helpers repeatedly on the same instance; do not introduce a synthetic `Collection` type or duplicate client/server handlers. The shared command must drive physical/logical capacities 100 -> 70 -> 60, seed retained storage with nonzero sentinels, and report logical capacity, physical capacity, buffer-reuse decisions, and shared-row preservation. Build-specific assertions and result fields stay behind compile-time guards: the client verifies `puiSounds` (the client-only member omitted from `SharedMembers()`) is zero across the physical layout, while the server verifies its build-local `Members() == SharedMembers()` path and shared values. This is runtime acceptance instrumentation, not a unit test or a second allocation implementation.

## Critical files

- `Engine/Source/Frame/Collections/Collection.h` — `Collection<T>`, `SharedCollectionRead`, and `CollectionRead` logical/physical capacity ownership.
- `Engine/Source/Frame/Collections/CollectionMemory.h` — `ResetDataToNull`, `AllocateAndAssign`, `Allocate`, and `GrowCapacityWithCopy` maintenance of the installed layout extent.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommands.cpp` — single shared fixture command with build-specific assertions/results behind `BT_CLIENT` / `BT_SERVER` guards.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.h` — real fixture collection: shared PostRender members in both builds and client-only `puiSounds`.
- `.agents/skills/agent-harness/SKILL.md` — fixture command contract used for client/server acceptance.

## Out of scope

- Reworking the already-completed allocation-failure handling or paired Interpolate/PostRender read-boundary validation.
- ID-map index validation or the real-stride deserialization byte ceiling; `CollectionReadIndexHardening.md` owns those independent trust-boundary gaps.
- Collection copy/persistence folds, Add/Remove/zero-initialization behavior, SOA member tuples, or collection leaf migrations.
- Wire format, save/replay format, `kiVersion`, `.pack`, CRC composition, or client/server affinity changes.
- Unit tests, broad deserialization fixtures, or unrelated allocation cleanup.

## Acceptance criteria

- The shared `collection_layout_capacity_fixture` command runs production deserialization on one reused `BlastersPostRender` and proves physical/logical capacities 100/100 -> 100/70 -> 100/60 without reallocating either shrink.
- On the client build, the command proves the 100 -> 70 -> 60 `SharedCollectionRead` sequence removes every seeded `puiSounds` sentinel across the complete physical layout, including rows 70-99, while preserving the exact shared Blasters rows read from the final stream.
- On the server build, the same command proves `BlastersPostRender::Members()` is the shared-only tuple, preserves the exact shared rows, reports logical capacity 60 and physical layout capacity 100, and performs no undersized reuse or out-of-bounds access.
- A subsequent read requiring more than 100 rows reallocates once, publishes the new physical capacity only after allocation succeeds, and retains the existing clean failure behavior.
- Debug x64 client and server builds pass; the one shared harness command passes on both executable endpoints with build-specific structured evidence. A known-good connected full-state/replay smoke reports no confirmed desync or checksum mismatch.
- Static inspection proves `iPhysicalLayoutCapacity` is maintained by every buffer install/reset/reuse path and is absent from serialization, CRC, difference logging, member tuples, and version calculations; `git diff --check` passes.

## Notes

- Risk trigger: this changes transient state in the generic collection base and both client/server reconstruction paths. The repair is storage-layout metadata only; valid logical bytes, stream order, CRC, and deterministic simulation state must remain unchanged.
- No `kiVersion` bump is expected because the new field is deliberately non-serialized and excluded from CRC. If implementation cannot preserve that boundary, stop rather than changing format under this plan.
- `CollectionReadIndexHardening.md` overlaps `Collection.h` but has an independent root cause and acceptance strategy. Reconcile current source before implementation; no ordering dependency or mandatory batching is required.
- No shader, `.pack`, threading, or allocation-tracked main-loop exposure beyond the existing collection allocation suppressions.
