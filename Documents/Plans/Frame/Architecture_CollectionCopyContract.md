# Architecture: Collection Copy/Zero-Init Contract

## Context
Source: /external-architecture-review on Engine/Source (recursive). The highest-drift-cost finding in Frame/Collections: per-collection `AllocateAndCopy` memcpy lists hand-restate the `Members()` tuple, the selective-copy persistence contract lives only in CLAUDE.md warnings ("a new persistent member must join the copy list or it silently resets"), and `Add()`'s zero-init-all-Interpolate-fields CRC invariant is enforced by checklist, not structure. A missed member in any of these is a silent persistence or CRC-desync bug.

## Design

### Engine/Source/Frame/Collections/CollectionMemory.h
- Add a `Members()`-fold full-copy helper (sibling to `AllocateAndCopyIds`, CollectionMemory.h:235-244) that copies every member in the tuple; migrate the five full-copy collections that hand-restate their member lists: `WindTrails.cpp:15-26`, `Sounds.cpp:15-28`, `SmokeTrails.cpp:15-27`, `AreaLights.cpp:15-28`, `HexShields.cpp:15-27` [~1h]
- Add a declarative `PersistentMembers()` sub-tuple convention for selective-copy collections so the persistence contract is code, not a CLAUDE.md warning; migrate `PointLights.cpp:15-26`, `Puffs.cpp:15-25`, `WindRadials.cpp:15-26` [~1h]

### Engine/Source/Frame/Collections/Collection.h
- Parameterize the ID stream in the `Add*IndexableElement` family (Collection.h:520-566 — the three variants differ only in ID source); `Sounds::Add` (`Sounds.cpp:40-47`) currently re-inlines `AddVisualIndexableElement`'s body solely because the UUID stream is hard-coded — migrate it to the shared helper [~30m]
- Add a generic paired-`Remove` helper (the identical 10-line ASSERT → fetch pair → `RemoveIndexableElement` → `rId = {}` body is copied in `WindTrailsUpdate.cpp:48-58`, `AreaLightsUpdate.cpp:53-63`, `BillboardsUpdate.cpp:52-62`, `PointLightsUpdate.cpp:120-130`, `HexShieldsUpdate.cpp:94-104`, `SmokeTrails.cpp:124-134`, `Sounds.cpp:56-66`) [~30m]
- Add a `Members()`-fold zero-init helper applied at the spawn index in `Add`, making the documented "Add() must zero-initialize all Interpolate fields (stale pre-first-Sync memory causes CRC desync)" invariant structural; migrate the seven hand-written zero-init blocks [~30m]

## Critical files
- `Engine/Source/Frame/Collections/CollectionMemory.h`, `Collection.h`
- All eleven collection subdirectories' core/Update TUs (listed above)
- `Engine/Source/Frame/Collections/CLAUDE.md` and per-collection CLAUDE.md files (update the copy-list warnings to name the new helpers)

## Out of scope
- The `ForEach*` opt-in-hook redesign (`Frame/Architecture_PhaseHookOptIn.md`)
- Controller-collection interpolate/spawn helpers and render-path duplication (`Frame/Architecture_CollectionHelperDedup.md`)
- `Collection.h` file-size reduction (649-line header — separate `/reduce-file` concern)
- Any change to CRC computation or serialization order

## Notes
- Invariant exposure: HIGH — this touches the cross-frame copy machinery on the CRC'd sim path. Every migration must be byte-identical: same members copied, same order, same widths. The exact-capacity vs reuse-if-large-enough distinction between `AllocateCore` (CollectionMemory.h:181-223) and `AllocateAndAssign` (:127-137) is CRC-mandated (`iCapacity` is CRC'd) — do not unify. Requires client/server cross-build parity check and a replay/CRC soak after landing
- Grill decision: `PersistentMembers()` sub-tuple vs per-member trait tag — pick the mechanism that keeps `Members()` the single source of truth
