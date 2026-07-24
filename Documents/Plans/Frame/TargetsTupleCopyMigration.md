<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Targets Tuple Copy Migration

## Context

The collection copy-contract implementation introduced `engine::AllocateAndCopyMembers` (`Engine/Source/Frame/Collections/CollectionMemory.h`), which calls the existing `engine::Allocate` path and then copies member rows in stable tuple and array-entry order — `PersistentMembers()` when the type defines one, otherwise every entry of `Members()`. `game::TargetsInterpolate::AllocateAndCopy` and `game::TargetsPostRender::AllocateAndCopy` remain manual because Targets was outside that plan's approved engine-leaf migration table; correctness and adversarial reviews classified this residual as out of that plan's scope, not an acceptance failure.

Both functions, in `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.cpp`, currently call `engine::Allocate(rCurrent, rPrevious, rCurrent.Members())` and then, under an `if (rCurrent.iCount > 0)` guard, `std::memcpy` every member of their complete `Members()` tuple row-by-array. In `Targets.h`, each type's `Members()` returns exactly its `SharedMembers()` tuple — `pVecPositions` for `TargetsInterpolate`; `puiIds`, `pFlags`, `puiSubscribers`, `pAlignments` for `TargetsPostRender` — and neither type defines `PersistentMembers()`. The code is correct today, but it duplicates the full-copy contract for CRC-participating game state and can drift when a member is added.

## Design

Replace each manual body with a single call to the existing helper:

- `TargetsInterpolate::AllocateAndCopy(rCurrent, rPrevious)` body becomes `engine::AllocateAndCopyMembers(rCurrent, rPrevious);` — deleting the explicit `engine::Allocate` call, the `iCount > 0` guard, and the `pVecPositions` memcpy.
- `TargetsPostRender::AllocateAndCopy(rCurrent, rPrevious)` body becomes `engine::AllocateAndCopyMembers(rCurrent, rPrevious);` — deleting the explicit `engine::Allocate` call, the `iCount > 0` guard, and the four memcpys (`puiIds`, `pFlags`, `puiSubscribers`, `pAlignments`).

Behavioral equivalence rationale (must remain true in the final diff):

- Neither Targets type defines `PersistentMembers()`, so `AllocateAndCopyMembers` takes its else-branch and copies every `Members()` entry — the same complete-member selection, live-row count (`iCount`), tuple order, and element widths as the removed memcpy list. Its `CopyMemberRows` already guards `iCount > 0`.
- The helper calls `engine::Allocate(rCurrent, rPrevious, rCurrent.Members())` exactly as the manual bodies do, so allocation metadata behavior — capacity and `TargetsInterpolate`'s `kIdToIndex` ID-to-index map — is unchanged.
- No behavioral or layout change: copied shared bytes and the resulting deterministic CRC must remain identical on client and server.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.cpp` — the two `AllocateAndCopy` function bodies to migrate.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h` — read-only authority for `SharedMembers()` / `Members()` tuple identity; not edited.
- `Engine/Source/Frame/Collections/CollectionMemory.h` — read-only authority for the `engine::AllocateAndCopyMembers` contract; not edited.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change below and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way.

In scope (the only regions that may change):

- `Targets.cpp` — the body of `TargetsInterpolate::AllocateAndCopy(TargetsInterpolate&, const TargetsInterpolate&)`.
- `Targets.cpp` — the body of `TargetsPostRender::AllocateAndCopy(TargetsPostRender&, const TargetsPostRender&)`.
- `Targets.cpp` — only the mechanical necessities the two body changes require (e.g., an include adjustment if compilation demands one); nothing else in the file.

Out of scope:

- Every other function, declaration, or region of `Targets.cpp` and all of `Targets.h` and `CollectionMemory.h`.
- Any broader collection migration or collection-framework sweep, including other collections' `AllocateAndCopy` bodies.
- Add/Remove behavior, ID generation, zero-initialization, transfer, update, render, or query behavior.
- Member tuple, SOA layout, `kiVersion`, serialization, save/replay format, CRC composition, wire protocol, or client/server affinity changes.
- Unit tests or unrelated cleanup.

## Risk tier and invariants

- Tier trigger: both copied tuples are wholly shared game state and participate in deterministic CRC behavior — a determinism/CRC-adjacent surface. The change is intended as a byte-equivalent refactor; any copied-byte, count, order, capacity, or ID-map difference is an acceptance failure.
- No `kiVersion` bump: accepted output bytes, serialization, and layout remain unchanged. If implementation cannot prove byte equivalence, stop rather than changing version or behavior under this plan.
- No `.pack`, shader, allocation-tracked hot-path, threading, trust-boundary, affinity, or wire-protocol exposure.

## Acceptance criteria

- Static inspection proves each migrated helper call selects the same complete `Members()` tuple, row count, element widths, and order as the removed `std::memcpy` list.
- Static inspection proves allocation metadata behavior is unchanged, including capacity and the Interpolate ID-to-index map.
- Debug x64 client and server builds pass.
- The smallest live client/server replay scenario first uses `query_collection` for `collection: "targets"` to prove `total >= 1`, then records at least 128 connected simulation ticks, replays them, and reports no confirmed desync or checksum-mismatch diagnostics. Exact CRC values need not be asserted; non-empty Targets coverage, unchanged source-level byte selection, and replay agreement are the acceptance signal.
- No tuple/layout/version/serialization, Add/Remove, or unrelated collection changes appear in the diff; `git diff --check` passes.
