# Collection Headers Quick-Wins Batch

## Context

Low-severity bundle from the 2026-07 design + adversarial review of `Engine/Source/Frame/Collections/{Collection.h, CollectionMemory.h, CollectionController.h, CollectionId.h}` — items individually too small for their own plans. Nine mechanical fixes: two tiny hardening ASSERTs/comment corrections and seven doc/dedup cleanups. None is a live desync source.

## Design

Each item is independent and local.

1. **A6 [LOW] — registration-time controller invariants.** `ControllerTypeRegistry::RegisterControllerType` (`CollectionController.h:103-109`) never asserts `rType.uiKeyframeCount >= 2` nor that `rType.pfTimes` is monotonic non-decreasing, yet `InterpolateKeyframes`' `NOLINT(clang-analyzer-security.ArrayBound)` suppression (`:74`) and `DestroyExpiredControlled` (`:137`) both rely on `uiKeyframeCount >= 2` and ordered times. Add one-time registration ASSERTs (`uiKeyframeCount` in `[2, kMaxControllerKeyframes]`, `pfTimes[0..uiKeyframeCount-1]` non-decreasing) so the suppression is honest. Startup-only, no runtime cost.

2. **A7 [LOW] — stale `// Heap:` comments name the wrong call.** `Collection.h:531-532`, `:549-550`, `:568-569` (the `AddIndexableElement` / `AddVisualIndexableElement` / `AddIndexableElementWithId` heap notes) say `unordered_map::operator[]` but the code uses `insert_or_assign`. Policy-mandated Heap comments must name the actual allocating call. Rider: the identical stale `operator[]` note at `OptionalIdToIndex::Read` (`:228`) has the same defect — fix it too.

3. **D2 [MED] — fold vestigial `AllocateCore` into `Allocate`.** `CollectionMemory.h:181-231`: `AllocateCore`'s `bool` return has no consumer (documented dead in the Collections hub AGENTS.md), and `Allocate` (:228-231) is its sole caller. Merge the body into `Allocate` (drop the unused `bool`); keep the `AllocateAndCopyIds` entry point unchanged. Update the "returning false when previous-frame data was null — a signal available to callers, with no current consumer" line in `Engine/Source/Frame/Collections/AGENTS.md`. (Do NOT touch the CRC-mandated `AllocateCore`-vs-`AllocateAndAssign` capacity-reuse distinction — that is a separate contract, see `Architecture_CollectionCopyContract.md`.)

4. **D3 [MED] — share one invalid-type-index sentinel.** `TypeRegistry::RegisterType` (`Collection.h:156-157`) hard-codes `0xFF` in its `ASSERT(ruiIndex == 0xFF)` / `ASSERT(sTypes.size() < 0xFF)`, while `ControllerTypeRegistry` (`CollectionController.h:105-106`) uses the named `kuiInvalidControllerType` (= 0xFF). Introduce one shared named sentinel (e.g. `kuiInvalidTypeIndex`) used by both registries' ASSERTs.

5. **D4 [LOW] — modernize the `HasIdToIndex` trait.** Replace the C++17 `void_t` `HasIdToIndex` trait (`CollectionMemory.h:168-176`) with a `requires`-concept matching the sibling `HasSharedMembers` (`Collection.h:376`). One use site: `AllocateCore` (`:190`, or `Allocate` after D2 folds it).

6. **D5 [LOW] — wrong "CRTP pattern" comment.** `Collection.h:196` calls the indexable `OptionalIdToIndex` specialization "using CRTP pattern" — false: `T` is the tag/element type, not a derived class. Reword.

7. **D6 [LOW] — mis-homed growth-formula comment.** `CollectionMemory.h:246` documents "Growth strategy: 2 * capacity + 1" on `GrowCapacityWithCopy`, which takes `iNewCapacity` as a parameter and does not compute it. The formula actually lives in `GrowPairedCollections` (`Collection.h:516`). Move/adjust the comment so the CRC-mandated deterministic growth formula is documented where it is computed.

8. **D7 [LOW] — formatter-location doc note.** `CollectionId.h:129-144` hosts `std::formatter<global_id_t>` while `Engine.h` hosts `std::formatter<uuid_t>` / `std::formatter<id_t<T>>`. Add one line in the `Engine.h` formatter comment noting `global_id_t`'s formatter lives with its type in `CollectionId.h` (doc-only; do not move the specialization).

9. **D8 [LOW] — verify then resolve the `MultiCrc` count guard asymmetry.** `MultiCrc` (`Collection.h:33`) guards `if (iCount > 0)` but `MultiWrite` (:60) and `MultiRead` (:85) do not. Verify `common::Crc` zero-count behavior; then either drop the `MultiCrc` guard (if `Crc(ptr, 0)` is well-defined and identity) or add a one-line comment justifying the asymmetry. (If `Frame/Refactor_CollectionMemberVisitor.md` lands first, resolve inside the shared visitor.)

## Critical files

- `Engine/Source/Frame/Collections/Collection.h` — items 2, 4, 6, 8, 9
- `Engine/Source/Frame/Collections/CollectionMemory.h` — items 3, 5, 7
- `Engine/Source/Frame/Collections/CollectionController.h` — items 1, 4
- `Engine/Source/Frame/Collections/CollectionId.h` — item 8
- `Engine/Source/Engine.h` — item 8 (formatter comment)
- `Engine/Source/Frame/Collections/AGENTS.md` — item 3 (AllocateCore dead-signal line)

## Out of scope

- The member-pointer visitor dedup (`Frame/Refactor_CollectionMemberVisitor.md`), copy/zero-init contract, phase-hook redesign, and deserialization memory-safety hardening — separate plans, same files.
- Any behavioral / CRC / wire / `kiVersion` change (all items are ASSERT-add, comment, or compile-time-identical dedup).

## Notes

- Items 1 and 9 add a startup ASSERT / possibly remove a redundant guard — verify `common::Crc(ptr, 0)` for item 9 before touching it. Everything else is comment or dead-signal cleanup. No runtime behavior change; no replay/CRC exposure beyond the item-9 guard which must stay byte-identical.
- Land within the Frame-collections series scheduling (Order.md Dependencies / File Group); refresh citations if `Refactor_CollectionMemberVisitor` lands first (it rewrites the `MultiCrc`/`MultiWrite`/`MultiRead` bodies item 9 cites).
