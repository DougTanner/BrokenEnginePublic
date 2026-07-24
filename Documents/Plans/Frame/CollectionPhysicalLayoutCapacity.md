<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Collection Physical Layout Capacity Retention

## Context

A post-reconciliation session audit found one unresolved shrink-reuse defect in the shared SOA deserialization framework after the collection deserialization hardening landed. The defect: `Collection<T>` (`Engine/Source/Frame/Collections/Collection.h`) tracked only the serialized/logical `iCapacity` alongside `iCount` and `pData`, with no transient field recording the element capacity used to stride the installed buffer. `SharedCollectionRead` captured the pre-`Read` `iCapacity` as the reuse bound, which survives one shrink but loses the true layout extent afterward: with a buffer physically strided for 100 rows, reads at logical capacities 70 then 60 reuse the same buffer, but after the first read `iCapacity` records 70, so the second read sizes its zero-fill for 70 while member pointers remain laid out at stride 100 — part of the client-only storage stays stale.

The completed deserialization work already owns allocation-failure cleanup and paired Interpolate/PostRender parity; this plan is specifically the missing persistent physical-layout extent. It predates the collection copy-contract session; the tuple-copy folds are compatible with the corrected allocation metadata and require no redesign.

Implementation status note: current source (verified 2026-07-24) already contains the `iPhysicalLayoutCapacity` field, its maintenance in `CollectionMemory.h`, the `SharedCollectionRead` physical-extent zero-fill, and the `collection_layout_capacity_fixture` command. The implementer makes only the deltas still missing against the Design below — where a step is already satisfied, verify it and make no edit; the deliverable is the acceptance evidence.

## Design

- One transient `int64_t iPhysicalLayoutCapacity = 0` field on `Collection<T>`, declared with `iCount`/`iCapacity`/`pData`, as the authoritative element capacity used to lay out the currently installed `pData` buffer.
- The field is maintained at every buffer ownership transition: set only after successful allocation in `AllocateAndAssign`, `Allocate`, and `GrowCapacityWithCopy` (never published before the install succeeds); preserved on buffer reuse (the `AllocateAndAssign` early-return and the equal-capacity `Allocate` path); cleared together with `pData` and `iCapacity` in `ResetDataToNull`. Positive-capacity empty-member layouts retain the same invariant.
- Full and shared collection deserialization use `iPhysicalLayoutCapacity` for allocation-reuse decisions (`AllocateAndAssign` compares it, not the just-read `iCapacity`, against the requested capacity). `SharedCollectionRead` sizes its complete-buffer zero-fill from `MemberTupleBufferSize(iPhysicalLayoutCapacity, fullMembers)`, never from a prior logical `iCapacity` snapshot.
- `iCapacity` stays the stream-visible logical capacity and CRC input. `iPhysicalLayoutCapacity` is derived storage state: excluded from `Write`, `Read`, `Crc`, `LogDifferences`, member tuples, serialization, and versioning. No `kiVersion` bump — if implementation cannot preserve that boundary, stop rather than change format under this plan.
- One debug-input-only `collection_layout_capacity_fixture` command in the shared dispatch of `Projects/BrokenEngineSandbox/Source/Agent/AgentCommands.cpp` (dispatched before the side-specific handlers, since that TU compiles into both executables; throws when `kbDebugInput` is disabled). It uses the real `BlastersPostRender` collection and invokes the production deserialization helpers repeatedly on the same instance — no synthetic `Collection` type, no duplicated client/server handlers. The command drives physical/logical capacities 100 -> 70 -> 60 (then one >100-row read, 150, to prove regrowth), seeds retained storage with nonzero sentinels, and reports logical capacity, physical capacity, buffer-reuse decisions, and shared-row preservation. Build-specific assertions and result fields stay behind `BT_CLIENT`/`BT_SERVER` guards: the client verifies `puiSounds` (the client-only member omitted from `SharedMembers()`) is zero across the physical layout; the server verifies its build-local `Members() == SharedMembers()` parity and shared values. This is runtime acceptance instrumentation, not a unit test or a second allocation implementation.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change satisfying the Design and Acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants no permission to touch anything in it beyond the named regions plus the mechanical necessities (includes, declarations) the named change requires.

### In scope

- `Engine/Source/Frame/Collections/Collection.h`
  - `Collection<T>` member declarations: the transient `iPhysicalLayoutCapacity` field and its exclusion from `Write`, `Read`, `Crc`, and `LogDifferences` on the same struct.
  - `SharedCollectionRead`: allocation-reuse via `AllocateAndAssign` and the complete-buffer zero-fill sized from `iPhysicalLayoutCapacity`.
  - `AllocateAndRead` (used by `CollectionRead`): only insofar as its `AllocateAndAssign`/`ResetDataToNull` calls carry the reuse decision — no other change.
- `Engine/Source/Frame/Collections/CollectionMemory.h`
  - `ResetDataToNull`: clear `iPhysicalLayoutCapacity` with `pData` and `iCapacity`.
  - `AllocateAndAssign`: reuse guard on `iPhysicalLayoutCapacity`, publish-after-success of capacity/layout/pData.
  - `Allocate`: publish-after-success on the reallocating path; preserve the field on the equal-capacity reuse path.
  - `GrowCapacityWithCopy`: publish the new physical capacity after the grown buffer is installed.
- `Projects/BrokenEngineSandbox/Source/Agent/AgentCommands.cpp`
  - The `collection_layout_capacity_fixture` handler (`CommandCollectionLayoutCapacityFixture` and its fixture-local helpers in the anonymous namespace) and its dispatch line in `ExecuteAgentCommand` ahead of the side-specific fallthrough.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/Blasters.h`
  - Read-only fixture dependency: `BlastersPostRender` shared members (`pFlags`, `pVecVelocities`, `pfPitches`, `pAlignments`) and client-only `puiSounds`. No edits to this file.
- `.agents/skills/agent-harness/SKILL.md`
  - Fixture command contract used for client/server acceptance, only if the command is not yet documented there; no other skill content.

### Out of scope

- Reworking the already-completed allocation-failure handling or paired Interpolate/PostRender read-boundary validation.
- ID-map index validation or the real-stride deserialization byte ceiling; `CollectionReadIndexHardening.md` owns those independent trust-boundary gaps.
- Collection copy/persistence folds, Add/Remove/zero-initialization behavior, SOA member tuples, or collection leaf migrations.
- Wire format, save/replay format, `kiVersion`, `.pack`, CRC composition, or client/server affinity changes.
- Unit tests, broad deserialization fixtures, or unrelated allocation cleanup.

## Risk tier and invariants

Tier 3 — the change touches transient state in the generic collection base and both client/server reconstruction paths, adjacent to serialization/data-layout and determinism surfaces. Invariants: the repair is storage-layout metadata only — valid logical bytes, stream order, CRC composition, and deterministic simulation state must remain byte-identical; `iPhysicalLayoutCapacity` never becomes stream-visible or CRC-visible; allocation failure never publishes a capacity or layout that was not installed. No shader, `.pack`, threading, or allocation-tracked main-loop exposure beyond the existing collection allocation suppressions (`ScopedSuppressAllocationTracking` + `// Heap:`).

`CollectionReadIndexHardening.md` overlaps `Collection.h` but has an independent root cause and acceptance strategy; reconcile current source before implementation. No ordering dependency or mandatory batching.

## Acceptance criteria

- The shared `collection_layout_capacity_fixture` command runs production deserialization on one reused `BlastersPostRender` and proves physical/logical capacities 100/100 -> 100/70 -> 100/60 without reallocating on either shrink.
- On the client build, the command proves the 100 -> 70 -> 60 `SharedCollectionRead` sequence removes every seeded `puiSounds` sentinel across the complete physical layout, including rows 70-99, while preserving the exact shared Blasters rows read from the final stream.
- On the server build, the same command proves `BlastersPostRender::Members()` is the shared-only tuple, preserves the exact shared rows, reports logical capacity 60 and physical layout capacity 100, and performs no undersized reuse or out-of-bounds access.
- A subsequent read requiring more than 100 rows (150 in the fixture) reallocates exactly once, publishes the new physical capacity only after allocation succeeds, and retains the existing clean failure behavior.
- Debug x64 client and server builds pass; the one shared harness command passes on both executable endpoints with build-specific structured evidence. A known-good connected full-state/replay smoke reports no confirmed desync or checksum mismatch.
- Static inspection proves `iPhysicalLayoutCapacity` is maintained by every buffer install/reset/reuse path and is absent from serialization, CRC, difference logging, member tuples, and version calculations; `git diff --check` passes.
