# Architecture: Collection Copy/Zero-Init Contract

## Context
Source: /external-architecture-review on Engine/Source (recursive). The highest-drift-cost finding in Frame/Collections: per-collection `AllocateAndCopy` memcpy lists hand-restate the `Members()` tuple, the selective-copy persistence contract lives only in AGENTS.md warnings ("a new persistent member must join the copy list or it silently resets"), and `Add()`'s zero-init-all-Interpolate-fields CRC invariant is enforced by checklist, not structure. A missed member in any of these is a silent persistence or CRC-desync bug.

## Design

### Engine/Source/Frame/Collections/CollectionMemory.h
- Add a `Members()`-fold full-copy helper (sibling to `AllocateAndCopyIds`, CollectionMemory.h:235-244) that copies every member in the tuple (handling both single-pointer and C-array-of-pointer tuple entries, like the existing sizing/swap folds); migrate the four collections whose memcpy list equals their full `Members()` tuple: `WindTrails.cpp:15-26`, `Sounds.cpp:15-28`, `SmokeTrails.cpp:15-27`, `AreaLights.cpp:15-28` [~1h]
- Add a declarative `PersistentMembers()` sub-tuple convention for selective-copy collections so the persistence contract is code, not an AGENTS.md warning; migrate the four selective-copy collections: `PointLights.cpp:15-26`, `Puffs.cpp:15-25`, `WindRadials.cpp:15-26`, `HexShields.cpp:15-27` (HexShields copies 5 of its 10 `Members()` — the transforms and directional-intensity arrays deliberately carry forward via the Interpolate `Update` instead; see the "asymmetric propagation" note in `HexShields/AGENTS.md`) [~1h]

### Engine/Source/Frame/Collections/Collection.h
- Parameterize the ID stream in the `Add*IndexableElement` family (Collection.h:520-566 — the three variants differ only in ID source); `SoundsPostRender::Add` (`SoundsUpdate.cpp:33-54`) currently re-inlines `AddVisualIndexableElement`'s body solely because it mints from the dedicated `GenerateSoundUuid()` stream (documented in `Sounds/AGENTS.md`) — the helper must take an ID-generation callable so that stream stays distinct; migrate it [~30m]
- Add a generic paired-`Remove` helper (the identical 10-line ASSERT → fetch pair → `RemoveIndexableElement` → `rId = {}` body is copied in `WindTrailsUpdate.cpp:48-58`, `AreaLightsUpdate.cpp:53-63`, `BillboardsUpdate.cpp:52-62`, `PointLightsUpdate.cpp:120-130`, `HexShieldsUpdate.cpp:94-104`, `SmokeTrails.cpp:124-134`, `SoundsUpdate.cpp:56-66`) [~30m]
- Add a `Members()`-fold zero-init helper applied at the spawn index in `Add`, making the documented "Add() must zero-initialize all Interpolate fields (stale pre-first-Sync memory causes CRC desync)" invariant structural; migrate the seven hand-written zero-init blocks (WindTrails, Sounds, SmokeTrails, AreaLights, HexShields, PointLights, Billboards). Non-zero defaults (type index, W=1 positions, `pfIntensityMultipliers = 1.0f` in AreaLights, spawn-time `pfStartTimes`, `kuiInvalidControllerType` = 0xFF sentinels) are written by the collection after the fold — net result must be byte-identical per site [~30m]

## Critical files
- `Engine/Source/Frame/Collections/CollectionMemory.h`, `Collection.h`
- The collection core/Update TUs listed above
- `Engine/Source/Frame/Collections/AGENTS.md` and per-collection AGENTS.md files (update the copy-list warnings to name the new helpers)

## Out of scope
- The `ForEach*` opt-in-hook redesign (`Frame/Architecture_PhaseHookOptIn.md`)
- Controller-collection interpolate/spawn helpers and render-path duplication (`Frame/Architecture_CollectionHelperDedup.md`)
- `Collection.h` file-size reduction (649-line header — separate `/reduce-file` concern)
- Any change to CRC computation or serialization order

## Notes
- Invariant exposure: HIGH — this touches the cross-frame copy machinery on the CRC'd sim path. Every migration must be byte-identical: same members copied, same order, same widths. The exact-capacity vs reuse-if-large-enough distinction between `AllocateCore` (CollectionMemory.h:181-223) and `AllocateAndAssign` (:116-138) is CRC-mandated (`iCapacity` is CRC'd) — do not unify. Requires client/server cross-build parity check and a replay/CRC soak after landing
- The eight engine collections listed are all `#if defined(BT_CLIENT)`-only (no `SharedMembers()`, no CRC participation), but the helpers land in the shared `Collection.h`/`CollectionMemory.h` templates that game-layer CRC'd collections also use — the HIGH rating reflects the shared machinery, not the engine leaves
- Grill decision: `PersistentMembers()` sub-tuple vs per-member trait tag — pick the mechanism that keeps `Members()` the single source of truth

## Verification Notes
- Verified against source 2026-07-02. Original draft counted HexShields among the full-copy collections — false: its `AllocateAndCopy` copies 5 of 10 `Members()` entries by design (asymmetric propagation), so it was moved to the `PersistentMembers()` camp. `Sounds::Add`/`Remove` citations corrected from `Sounds.cpp` to `SoundsUpdate.cpp`
