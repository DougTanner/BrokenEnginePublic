# Architecture: Collection Header Cohesion and Split

## Context

Source: /external-deep-analysis on `Engine/Source/Frame/Collections` (non-recursive) — architecture cohesion findings plus the refactor-clean file-size triage. `Collection.h` is 767 lines, over the 500-line header guideline, and hosts at least five concerns: ID/identity types with their std hash/formatter specializations (lines 37-173), tuple serialization/CRC helpers (186-279 and 599-704), the keyframe controller system (294-393, 744-765), type registries with asset-I/O side effects (395-444), and the `Collection`/`OptionalIdToIndex` base (446-592) plus client-render bookkeeping (706-742).

Responsibility also bleeds between the pair: `CollectionMemory.h` ("SOA memory templates") contains entity-lifecycle and identity logic — the `AddIndexableElement` family and `RemoveIndexableElement` (`CollectionMemory.h:296-407`) depend on `FramePostRenderBase`, `TInterpolate::id_t`, and `idToIndexMap`, all concepts defined in `Collection.h`, which itself includes `CollectionMemory.h` *mid-file* at line 175. The concept-level circularity is papered over by template two-phase lookup; understanding "how is an element added" bounces between both files plus `FrameBase.h`.

## Design

### Engine/Source/Frame/Collections/Collection.h
- Run `/reduce-file Engine/Source/Frame/Collections/Collection.h`, feeding it this grouping input as the starting point [~1h with the items below]:
  - ID/identity cluster → own header (e.g., `CollectionId.h`): `global_id_t`, `uuid_t`, `id_t<T>`, the std hash/formatter specializations (lines 37-173)
  - Keyframe controller cluster → own header (e.g., `CollectionController.h`): `ControllerKeyframe`, `ControllerType`, `InterpolateKeyframes`, `ControllerTypeRegistry`, `DestroyExpiredControlled` (lines 294-393, 744-765)
  - Core stays: serialization/CRC helpers, `TypeRegistry` + asset-I/O port declarations, `CollectionFlags`, `OptionalIdToIndex`, `Collection`, collection-level pattern helpers
- Move the `#include "CollectionMemory.h"` from line 175 to the top of the file — no ordering requirement forces the mid-file position (`CollectionMemory.h` forward-declares its own `FramePostRenderBase` and uses nothing from `Collection.h`'s front matter) [~5m]
- Group the client-only render helpers `AccumulateRenderCapacity` / `EraseStaleRenderState` (lines 706-742) under `#if defined(BT_CLIENT)` for consistency with `AddVisualIndexableElement` (`CollectionMemory.h:352-367`) — today they rely solely on non-instantiation [~10m]

### Engine/Source/Frame/Collections/CollectionMemory.h
- Move the indexable lifecycle helpers (`AddElement`, `GrowPairedCollections`, `AddIndexableElement`, `AddVisualIndexableElement`, `AddIndexableElementWithId`, `RemoveIndexableElement` — lines 296-407 — and `DestroyElement` — lines 439-452) into the collection-side header chosen by the split, leaving `CollectionMemory.h` purely sizing/alignment/allocation/`SwapElement` (`SwapElement` at lines 413-437 stays) [~30m]

## Critical files
- `Engine/Source/Frame/Collections/Collection.h`
- `Engine/Source/Frame/Collections/CollectionMemory.h`
- New headers from the split (add to engine vcxproj filters; every collection header includes `Collection.h`, so consumers follow automatically if `Collection.h` aggregates the new headers)

## Out of scope
- Any behavior change — this is a pure reorganization; all symbols keep their definitions byte-identical
- The member-tuple protocol, `extern template` scheme, and `SharedMembers()` conventions
- The `AllocateAndAssign` guard fix, count validation, CRC mixing, and registry asserts (content plans in this directory — land them **before** this split so their line citations stay meaningful; see Order.md Dependencies)

## Notes
- No determinism/CRC/`kiVersion` exposure (declarations move between headers; definitions unchanged). Both client and server builds must compile after the move; `Collection.h` should keep including the split-out headers so the 19 existing include sites need no edits.
- The split also resolves the formatter-ownership oddity in passing if `/reduce-file` chooses to: `std::formatter<global_id_t>` lives here while `formatter<uuid_t>`/`formatter<id_t<T>>` live in `Engine.h` — co-locating them in the ID header is optional and low-value; do not force it.

## Verification Notes (2026-06-11)
- **Two corrections made**: (1) `Collection.h` is 767 lines, not 667 (strengthens the case — further over the guideline). (2) The lifecycle-helper move cite said "lines 296-452", which would have swept `SwapElement` (:413-437) into the move despite the same sentence keeping it in `CollectionMemory.h`; corrected to 296-407 plus `DestroyElement` at 439-452.
- The five concern ranges in Context verified against current source: IDs/hash/formatter 37-173, serialization/CRC helpers 186-279 and 599-704, keyframe controller 294-393 + 744-765, registries 395-444, `Collection`/`OptionalIdToIndex` 446-592, client-render bookkeeping 706-742. Mid-file `#include "CollectionMemory.h"` confirmed at :175.
- Include-move claim verified by inspection: `CollectionMemory.h` forward-declares its own `FramePostRenderBase` (:8) and uses `id_t` only as the dependent `typename TInterpolate::id_t` — nothing from `Collection.h`'s front matter is named non-dependently, so a top-of-file include should compile (final proof is the build, as the plan already requires).
- "19 existing include sites" confirmed (grep: 19 source files include `Collections/Collection.h`).
- `AccumulateRenderCapacity`/`EraseStaleRenderState` (:706-742) confirmed unguarded today (non-instantiation only) vs `AddVisualIndexableElement`'s explicit `#if defined(BT_CLIENT)` (`CollectionMemory.h:352-367`).
- Caveat for execution ordering: `Engine/DeadCodeAndUnusedIncludesSweep.md` items 10/13/14 delete dead forward decls and the `VkDeviceSize` alias from this same header's front matter, and item 12 retargets the `CollectionMemory.h:3` include — land those (or fold them in) before/with the split so the new ID header isn't created with the dead lines.
