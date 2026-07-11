# Refactor: Collect SOA Member-Pointer Visitor Into One Leaf Helper

## Context

Design review (2026-07) of `Engine/Source/Frame/Collections/{Collection.h, CollectionMemory.h}`. The array-vs-single-pointer `if constexpr (std::is_array_v<...>)` branch — "for a C-array-of-pointers member iterate its N entries, else treat the member as a single pointer" — is hand-written 8× across the two headers, each copy re-deriving `std::extent_v` / `std::remove_extent_t` / `std::remove_pointer_t` and re-writing the same two-branch body:

- `Collection.h`: `MultiCrc` (:35-52), `MultiWrite` (:62-79), `MultiRead` (:87-104)
- `CollectionMemory.h`: `CalculateBufferSize` (:12-31), `AssignAligned` (:39-64), `AssignAndCopyAligned` (:67-104), `ResetDataToNull` (:148-165), `SwapElement` (:275-293)

Every future SOA operation re-implements the same branch, and a divergence between copies (e.g. an off-by-one in the array loop) is a silent layout/CRC hazard.

## Design

Add one `ForEachMemberPointer(member, fn)` leaf-visitor to `CollectionMemory.h` that resolves the array-vs-single-pointer branch once and invokes `fn` per underlying element pointer (passing the element-pointer reference, and, where needed, the array element size / index). Rewrite each of the 8 bodies so its per-element work becomes a one-line lambda over the shared visitor — the CRC accumulate, the stream write/read, the buffer-size sum, the aligned assign/copy, the null-reset, and the swap.

Compile-time restructuring only: identical member visitation ORDER and identical per-element arithmetic must be preserved (the CRC fold order, the `RoundUp<64>` alignment stride advance, and the `iCapacity`/`iCount` multipliers are all CRC- and layout-load-bearing). No call-site changes outside these two headers; the tuple-fold callers (`std::apply` over `Members()`) are untouched.

## Critical files

- `Engine/Source/Frame/Collections/CollectionMemory.h` — new `ForEachMemberPointer`; rewrite `CalculateBufferSize`, `AssignAligned`, `AssignAndCopyAligned`, `ResetDataToNull`, `SwapElement`
- `Engine/Source/Frame/Collections/Collection.h` — rewrite `MultiCrc`, `MultiWrite`, `MultiRead`
- `Engine/Source/Frame/Collections/AGENTS.md` — the "every helper (sizing, alignment, CRC, serialize, swap) handles both forms" line can point at the single visitor

## Out of scope

- The copy/zero-init contract helpers (`Frame/Architecture_CollectionCopyContract.md`), controller/render dedup (`Frame/Architecture_CollectionHelperDedup.md`), and the `ForEach*` phase-hook redesign (`Frame/Architecture_PhaseHookOptIn.md`) — different duplication, separate sessions, same files.
- The `AllocateCore` vs `AllocateAndAssign` capacity-reuse distinction (CRC-mandated — do not unify; see `Architecture_CollectionCopyContract.md`).
- Any change to member visitation order, arithmetic, or the `Members()`/`SharedMembers()` tuple contents.

## Acceptance criteria

- The 8 hand-written `is_array_v` branches collapse to one visitor + 8 one-line lambda bodies.
- Byte-identical output: CRC unchanged for known-good frames; save/replay round-trips identically; client/server cross-build parity holds.

## Invariant exposure

- Touches the SOA member-visitation core that both the CRC'd game collections and the client-only engine leaves use. The change is compile-time-only and byte-identical by construction, but a mistake shifts layout or CRC — requires a client/server cross-build parity check and a replay/CRC soak. No wire / `kiVersion` / `.pack` change.

## Notes

- Land within the Frame-collections series scheduling (Order.md Dependencies / File Group) — never interleave; refresh citations between sessions.
