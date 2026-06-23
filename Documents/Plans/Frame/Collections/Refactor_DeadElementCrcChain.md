# Refactor: Dead Per-Element CRC Chain (MultiElementCrc / SharedCollectionElementCrc)

## Context

Surfaced by the `/next-plan` Step 6 sibling sweep while executing `Refactor_DeadReallocateIfCapacityChanged` (same dead-helper + stale-doc pattern, different symbols). Two template helpers in `Engine/Source/Frame/Collections/Collection.h` form a dead chain:

- `SharedCollectionElementCrc` (`Collection.h:674-681`) has **zero call sites repo-wide** — a grep across `Engine/` + `Projects/` returns only its own definition, the `Collections/CLAUDE.md:9` doc line, and plan-file mentions. It was the **old per-element desync-diagnosis mechanism**, now fully superseded by the `LogDifferences()` family (per-field `common::LogDifference<...>`, e.g. `Explosions.cpp:319-348`, driven from `FrameBase.cpp:24-101`).
- `MultiElementCrc` (`Collection.h:603-626`) is called **only** from inside the dead `SharedCollectionElementCrc` (`:678`, `:680`). It becomes newly-dead the moment `SharedCollectionElementCrc` is deleted — kept alive today solely by the other dead helper.

This is **distinct from** the live per-collection CRC helpers `MultiCrc` / `CollectionCrc` / `SharedCollectionCrc`, which have real callers on the determinism CRC path and are **not** touched here.

Separately, `Engine/Source/Frame/Collections/CLAUDE.md:9` (the `Collection<T>` bullet) ends: *"…per-element desync diagnosis is `MultiElementCrc` / `SharedCollectionElementCrc`"* — a stale claim naming the dead mechanism. Actual per-element desync diagnosis is performed by `LogDifferences()`.

## Critical cross-plan conflict

`Architecture_CollectionCrcMixing.md` (currently queued in `Order.md`) **modifies `MultiElementCrc`** — its premise is to convert the collection-internal CRC XOR to an ordered fold, assuming `MultiElementCrc`/`SharedCollectionElementCrc` stay **live**. This plan deletes them. **The two plans cannot both land as written.** Resolution:

- Land this plan **first**. Then amend `Architecture_CollectionCrcMixing.md`: drop its `MultiElementCrc` / `SharedCollectionElementCrc` items (those symbols no longer exist). Its CRC-mixing concern for the *live* helpers (`MultiCrc` / `CollectionCrc` / collection-internal folds with real callers) survives if any genuine XOR-cancellation hazard remains there; re-verify at that plan's execution. If deleting the dead chain leaves `CollectionCrcMixing` with no live target, close it.

## Design

Like the originating `Refactor_DeadReallocateIfCapacityChanged`, deleting documented (if dead) CRC helpers brushes the root-CLAUDE.md "never remove working features without confirmation" rule — resolve keep-vs-delete at grill. Recommended path (KISS/YAGNI — zero callers, superseded mechanism):

### Engine/Source/Frame/Collections/Collection.h
- Delete `SharedCollectionElementCrc` (`:674-681`) [~5m]
- Delete `MultiElementCrc` (`:603-626`) — newly-dead once the above goes; verify no other caller appeared at execution [~5m]

### Engine/Source/Frame/Collections/CLAUDE.md
- Rewrite the `Collection<T>` bullet (`:9`) tail so it no longer claims per-element desync diagnosis runs through `MultiElementCrc` / `SharedCollectionElementCrc`; state that per-element desync diagnosis is the `LogDifferences()` family (per-field `common::LogDifference<...>`) [~5m]

### Documents/Plans/Graphics/... — coordinate
- Amend `Architecture_CollectionCrcMixing.md` per the cross-plan-conflict section above (drop dead-symbol items; close if no live target remains)

## Critical files
- `Engine/Source/Frame/Collections/Collection.h`
- `Engine/Source/Frame/Collections/CLAUDE.md`
- `Documents/Plans/Frame/Collections/Architecture_CollectionCrcMixing.md` (reconcile, not code)

## Out of scope
- The live per-collection CRC helpers `MultiCrc` / `CollectionCrc` / `SharedCollectionCrc` (real callers on the determinism path — untouched)
- The `LogDifferences()` family itself (the surviving desync-diagnosis mechanism)
- `ReallocateIfCapacityChanged` + `CollectionMemory.h:177`/`CLAUDE.md:10` (the originating `Refactor_DeadReallocateIfCapacityChanged` plan)
- The `Architecture_HeaderCohesionAndSplit.md` Collection.h split and the `AllocateAndAssign` reuse-guard bug (their own plans)
- Any change to the live CRC value, serialized format, or `kiVersion`

## Acceptance criteria
- `SharedCollectionElementCrc` and `MultiElementCrc` no longer exist in `Collection.h` (Option delete); both compile (client + server) with no new warnings.
- A repo-wide grep for either symbol returns no source reference.
- `CLAUDE.md:9` no longer names them; describes `LogDifferences()` as the per-element desync mechanism.
- `Architecture_CollectionCrcMixing.md` is reconciled (dead-symbol items dropped or the plan closed).

## Notes
- **Decision plan (present options).** Grill: delete the dead chain (recommended) vs keep it as a deliberate future API + fix only the doc (root never-remove-features rule, mirroring the `ReallocateIfCapacityChanged` A/B framing).
- **Invariant exposure**: the helpers live in the CRC infrastructure header but are **dead** (zero callers) — deleting uncalled templates emits no code and changes no live CRC value. No determinism/CRC-value/`kiVersion`/`.pack`/replay/guard exposure. The only real coupling is the **plan-level** conflict with `Architecture_CollectionCrcMixing.md`, handled above. Risks 0 (dead code + doc + plan reconciliation).
- Verify at execution that no new caller of either symbol appeared since authoring (re-grep both); if one exists, the finding is partially stale — re-scope.
