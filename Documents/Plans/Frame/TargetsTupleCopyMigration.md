<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Targets Tuple Copy Migration

## Context

The collection copy-contract implementation introduced `engine::AllocateAndCopyMembers`, which allocates through the existing `engine::Allocate` path and copies the selected member tuple in stable tuple and array-entry order. `game::TargetsInterpolate::AllocateAndCopy` and `game::TargetsPostRender::AllocateAndCopy` remain manual because Targets was outside that plan's approved engine-leaf migration table.

Both functions still call `engine::Allocate` and then hand-copy every live row from their complete `Members()` tuple in `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.cpp` (`TargetsInterpolate::AllocateAndCopy`, `TargetsPostRender::AllocateAndCopy`). In `Targets.h`, each `Members()` is exactly its complete `SharedMembers()` tuple: `pVecPositions` for Interpolate, and `puiIds`, `pFlags`, `puiSubscribers`, `pAlignments` for PostRender. The code is correct today, but it retains a second declaration of the full-copy contract for CRC-participating game state and can drift when a member is added.

This residual was proven during propagation of the collection copy-contract implementation; correctness and adversarial reviews classified it as outside the approved engine-leaf migration scope, not an acceptance failure.

## Design

- Replace the manual `engine::Allocate` plus `std::memcpy` bodies of both Targets `AllocateAndCopy` functions with `engine::AllocateAndCopyMembers(rCurrent, rPrevious)`.
- Preserve the exact live-row count, tuple order, element widths, and complete-member selection. Both types have no `PersistentMembers()` override, so the helper must copy every entry of `Members()`.
- Preserve `engine::Allocate` metadata behavior exactly, including capacity and `TargetsInterpolate`'s ID-to-index map.
- Make no behavioral or layout change: the copied shared bytes and resulting deterministic CRC must remain identical on client and server.

## Critical files

- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.cpp` — `TargetsInterpolate::AllocateAndCopy`, `TargetsPostRender::AllocateAndCopy`.
- `Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/Targets.h` — authoritative `SharedMembers()` / `Members()` tuple identity.
- `Engine/Source/Frame/Collections/CollectionMemory.h` — existing `engine::AllocateAndCopyMembers` contract.

## Out of scope

- Any broader collection migration or collection-framework sweep.
- Add/Remove behavior, ID generation, zero-initialization, transfer, update, render, or query behavior.
- Member tuple, SOA layout, `kiVersion`, serialization, save/replay format, CRC composition, wire protocol, or client/server affinity changes.
- Unit tests or unrelated cleanup.

## Acceptance criteria

- Static inspection proves each migrated helper call selects the same complete `Members()` tuple, row count, element widths, and order as the removed `std::memcpy` list.
- Static inspection proves allocation metadata behavior is unchanged, including capacity and the Interpolate ID-to-index map.
- Debug x64 client and server builds pass.
- The smallest live client/server replay scenario first uses `query_collection` for `collection: "targets"` to prove `total >= 1`, then records at least 128 connected simulation ticks, replays them, and reports no confirmed desync or checksum-mismatch diagnostics. Exact CRC values need not be asserted; non-empty Targets coverage, unchanged source-level byte selection, and replay agreement are the acceptance signal.
- No tuple/layout/version/serialization, Add/Remove, or unrelated collection changes appear in the diff; `git diff --check` passes.

## Notes

- Risk trigger: both copied tuples are wholly shared game state and participate in deterministic CRC behavior. This is intended as a byte-equivalent refactor; any copied-byte, count, order, capacity, or ID-map difference is an acceptance failure.
- No `kiVersion` bump is expected because accepted output bytes, serialization, and layout remain unchanged. If implementation cannot prove byte equivalence, stop rather than changing version or behavior under this plan.
- No `.pack`, shader, allocation-tracked hot-path, threading, trust-boundary, affinity, or wire-protocol exposure.
