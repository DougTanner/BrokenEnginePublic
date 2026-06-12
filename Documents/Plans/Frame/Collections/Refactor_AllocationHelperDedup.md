# Refactor: Allocate / ReallocateIfCapacityChanged Dedup

## Context

Source: /external-refactor-clean on `Engine/Source/Frame/Collections` (non-recursive). `ReallocateIfCapacityChanged` (`CollectionMemory.h:181-220`) and `Allocate` (`CollectionMemory.h:224-261`) are near-identical: both copy `iCount` and `idToIndexMap` from the previous frame, both `ResetDataToNull` when previous data is null, both reallocate-and-reassign on capacity mismatch. They differ only in (a) the return signal (`bool` consumed by leaf `Update()` early-returns vs. `void`) and (b) the reallocation condition — `Allocate` adds `|| rCurrent.pData == nullptr` (line 245) where `ReallocateIfCapacityChanged` checks capacity alone (line 202). The extra null check is unreachable-by-invariant (null `pData` implies zero capacity via `ResetDataToNull`, lines 143-146), but the asymmetry is a maintainer trap: a fix applied to one copy can silently miss the other. DRY violation per root CLAUDE.md.

## Design

### Engine/Source/Frame/Collections/CollectionMemory.h
- Extract the shared core into a single helper (e.g., `AllocateCore(rCurrent, rPrevious, members) -> bool` returning whether previous data existed); reimplement `ReallocateIfCapacityChanged` and `Allocate` as thin wrappers on it, preserving each caller-visible contract exactly — including the `false` early-return signal leaf `Update()` phases consume and the `ScopedSuppressAllocationTracking` + `// Heap:` discipline [~30m]
- Resolve the condition asymmetry deliberately: either keep the `pData == nullptr` term in the shared core (harmless, belt-and-suspenders) or drop it with a comment stating the invariant — pick one, don't preserve the divergence [~5m]

## Critical files
- `Engine/Source/Frame/Collections/CollectionMemory.h`

## Out of scope
- `AllocateAndAssign` and its reuse-guard bug (`Architecture_AllocateAndAssignReuseGuard.md`, same file — co-schedule; that plan may delete the reuse branch, which does not overlap this dedup)
- Any contract change visible to leaf collections

## Notes
- Mechanical and compile-checked; per-frame allocation path, so behavior must be byte-identical (same allocations, same copies, same return values). No determinism/CRC exposure when the contract is preserved.

## Verification Notes (2026-06-11)
- Near-duplication verified verbatim: `ReallocateIfCapacityChanged` (`CollectionMemory.h:181-220`) and `Allocate` (:224-261) share the iCount copy, `idToIndexMap` copy, null-previous `ResetDataToNull` early-out, and the reallocate-and-`AssignAligned` block (including the trailing `ASSERT(iCount <= iCapacity)` at :215/:258 — the shared core must keep it).
- Condition asymmetry confirmed: `!= iCapacity` at :202 vs `!= iCapacity || rCurrent.pData == nullptr` at :245.
- Unreachable-by-invariant claim verified: `ResetDataToNull` (:145-146) is the only path that leaves `pData` null and it zeroes `iCapacity` in the same breath; every allocating path sets both together — so `pData == nullptr` with matching nonzero capacity cannot occur. The plan's "resolve deliberately, don't preserve the divergence" framing is right.
- Return-contract difference (bool consumed by leaf `Update()` early-returns vs void) matches the Collections CLAUDE.md documented contract. No corrections needed.
