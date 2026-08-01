<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-25T16:04:48.760Z","dependsOn":[]} -->
# Island Placement Arena Capacity

## Context

`engine::kiMaxPlacementsPerTemplate` is 1024 and sizes a fixed per-template SSBO slab: the placement arena is `miTemplateCount * kiMaxPlacementsPerTemplate` entries of `shaders::AxisAlignedQuadLayout` (60 bytes), allocated once per framebuffer instance (`kiMaxFramebuffers = 4`). At the captured 70-template set that is 17,203,200 bytes (16.40625 MiB), and it scales with template count up to the `shaders::kiMaxIslands = 128` cap.

That per-template slab has no proof it covers every allowed client state. `Islands.cpp` bakes `firstInstance = iTemplate * kiMaxPlacementsPerTemplate` once at boot, so each template is capped at 1024 regardless of how few placements the other templates use. At placement 1025 the `EmitPlacement` capacity guard runs `ASSERT(false)`, and `common::Assert` throws unconditionally in every build config, aborting the whole `UpdateActiveIslands` call. Its comment claims the overflow "skips extra placements"; the `return` after the throwing assert is dead, so the comment is wrong. No overflow has been observed; this is an unbounded-by-current-capacity correctness risk, not a reported runtime failure.

The per-template shape is also the wrong shape. `game::Game::UpdateActiveIslands` emits only cells passing `iConfirmedTick >= 0 || rCoord == mClientGridCoord`; confirmed coordinates are bounded by `game::NetworkSessionContract::kiCoordSlots` (16) because released slots erase their coordinate frames, and the client's own cell adds at most one unconfirmed cell. `IslandChainPlacement` bounds one cell at 107 placements (`kiMaxIslandsPerCell` = `kiMaxBigIslands` 6 + 6 x `kiMaxSurroundSlots` 16 + `kiTailSmallCount` 5). So the **global** placement total across all templates can never exceed 17 x 107 = 1819, while the current arena reserves 71,680 entries. Roughly 97% of the arena is unreachable, and the two elevation prepasses draw the entire reserved arena — `miTemplateCount * kiMaxPlacementsPerTemplate` = 71,680 instances — every frame.

Packing placements into one shared arena with per-frame `firstInstance` therefore removes the overflow risk, cuts resident memory by about 16 MiB, and cuts prepass instance count by about 39x, without giving up record-once command buffers.

## Design

Replace the fixed per-template slab with a single compact arena sized by the proven global bound, and move `firstInstance` from a boot-baked constant to a per-frame write.

1. Promote the placement-count contract from `IslandChainPlacement.cpp`'s anonymous namespace to `IslandChainPlacement.h` as `engine::` constants, so the per-cell maximum becomes a header-level compile-time contract. `kiMaxIslandsPerCell` already exists with exactly the required derivation and value; move it and the constants it derives from rather than introducing a second name. The header is compiled into both client and server, so the promoted set stays constants only — no new dependency on Graphics or any client-only symbol, and no change to generation counts, placement order, or deterministic layout.
2. Replace `kiMaxPlacementsPerTemplate` in `Islands.h` with a global `kiMaxActivePlacements`, *derived* rather than a literal: `(game::NetworkSessionContract::kiCoordSlots + 1) * kiMaxIslandsPerCell` = 1819. `Islands.h` is client-only and the game PCH includes `Network/NetworkSessionContract.h` before `Engine.h`, so the game contract is already reachable; `Islands.cpp` already consumes `game::` symbols. Deriving the bound keeps the proof live: raising the coordinate-slot count or a placement constant resizes the arena automatically instead of silently invalidating a literal.
3. Size the four boot-allocated SSBO instances to `kiMaxActivePlacements` entries — independent of `miTemplateCount` — and stop baking `firstInstance` at boot.
4. In `UpdateActiveIslands`, add a counting pass over the active cells that accumulates per-template mesh-visible and total placement counts into workbuffer arrays, exclusive-prefix-sum those totals into per-template base offsets, then run the existing mesh-visible and offscreen emit passes writing at `base + localSlot`. Write both `firstInstance = base` and `instanceCount = meshVisibleCount` into the per-framebuffer indirect buffer each frame. Each template's run stays contiguous and mesh-visible-first, because one `vkCmdDrawIndexedIndirect` draws `instanceCount` instances from `firstInstance` while the prepasses consume the whole arena.
5. Keep a hard capacity guard, now against the global arena bound instead of a per-template one, at the same point in the emit path.
6. Reduce `mLastWrittenCounts` from a per-template vector to one per-framebuffer written total, and keep clearing the tail from the current total to the previous total so stale entries are not drawn by the prepasses.
7. Point the two record-once prepass instance counts at `kiMaxActivePlacements`, which stays boot-fixed, so the Global and Main command buffers remain recorded once.

No shader change is required: `Terrain.vert` and `QuadsAxisAlignedVisibleArea.vert` index only `pQuads[gl_InstanceIndex]` and assume no per-template stride. No descriptor change is required: the storage-buffer descriptors bind `offset = 0`, `range = VK_WHOLE_SIZE`. The indirect buffer is already per-framebuffer, host-visible, host-coherent, persistently mapped, and already written every frame, so per-frame `firstInstance` writes add no barrier, no synchronization change, and no buffer re-creation. Per-frame counting and prefix-sum use `gpThreadLocal->mWorkbuffer`, so no heap allocation enters the allocation-tracked main loop.

Resident SSBO memory goes from 17,203,200 bytes (16.40625 MiB) at 70 templates to 4 x 1819 x 60 = 436,560 bytes (426.33 KiB), and stops scaling with template count. Prepass instance count goes from 71,680 to 1,819.

## Critical files

- `Engine/Source/Frame/IslandChainPlacement.h` — receives the promoted per-cell placement contract.
- `Engine/Source/Frame/IslandChainPlacement.cpp` — constants move out of the anonymous namespace; existing reserve and assert sites keep consuming them.
- `Engine/Source/Graphics/Islands.h` — arena constant, member shape, and comments.
- `Engine/Source/Graphics/Islands.cpp` — arena allocation, per-frame counting/prefix-sum/emit, per-frame indirect writes, capacity guard, tail clear.
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` — prepass instance counts.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — comment describing the old fixed per-template slab.
- `Engine/Source/Graphics/Managers/DeviceManager.cpp` — comment describing the boot-baked `firstInstance`.

## In scope

- `Engine/Source/Frame/IslandChainPlacement.h` — add `engine::kiMaxIslandsPerCell` and the constants it derives from (`kiChainLargeCount`, `kiChainMediumCount`, `kiMaxBigIslands`, `kiMaxSurroundSlots`, `kiTailSmallCount`), moved verbatim in value from `IslandChainPlacement.cpp`.
- `Engine/Source/Frame/IslandChainPlacement.cpp` — remove those definitions from the anonymous namespace; existing uses at the surround-slot clamp, the `placedHullStorage`/`placedHullViews`/`rOut` reserve site, and the `placedHullStorage` capacity assert keep referring to the same names. Generated placements, counts, and order are unchanged.
- `Engine/Source/Graphics/Islands.h` — replace `kiMaxPlacementsPerTemplate` with derived `kiMaxActivePlacements`; change `mLastWrittenCounts` to a per-framebuffer written total; rewrite the capacity comment to state the derived bound, the contiguous-run and mesh-visible-prefix invariants, and the resident cost.
- `Engine/Source/Graphics/Islands.cpp` — size the storage buffers to `kiMaxActivePlacements`; drop the boot `firstInstance` bake; in `UpdateActiveIslands` add the per-template counting pass and exclusive prefix sum in workbuffer arrays, rebase both emit passes onto per-template offsets, write `firstInstance` and `instanceCount` per template per frame, replace the per-template capacity guard with the global-arena guard and correct its misleading comment, and clear the tail against the previous written total.
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` — the ShadowElevation and TerrainElevation `RecordDraw` instance counts become `kiMaxActivePlacements`.
- Comment-only corrections at the two sites that describe the old fixed per-template slab: `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` and `Engine/Source/Graphics/Managers/DeviceManager.cpp`.

## Out of scope

- Changing `kiDesiredCoordSlots`, `kiCoordSlots`, sticky duration, confirmation/adoption, `mActiveCoords`, `ComputeActiveSet` membership, or any client/server wire or subscription behavior.
- Changing island generation rules, deterministic layout, CRC/replay behavior, `.pack` data, asset bucket selection, or any server-reachable behavior.
- Runtime SSBO re-allocation or growth after boot, descriptor re-binding, device-idle waits, and any command-buffer re-record outside the existing recreation pipeline.
- Shader source changes, vertex/index buffer changes, pipeline or descriptor-layout changes, and changes to the mesh-residency path in `WriteMeshIndirect`.
- Reworking the other resident-memory buckets listed in `Documents/Investigations/Graphics/IslandResidentMemoryScaling_Overview.md`.
- Fixing the stale "triple-buffered" comments in `Islands.h` and `Managers/BufferManager.h`, and adding a size `static_assert` to `shaders::AxisAlignedQuadLayout`.

## Risk tier and invariants

Tier 3 trigger: the change alters the CPU/GPU placement-arena layout contract consumed by record-once command buffers, and its capacity proof draws on independently owned Network subscription and Frame placement contracts. The runtime change is client-render-only, with no determinism, CRC, wire, or persisted-format exposure.

- Server placement output is bit-identical: `IslandChainPlacement` changes are constant relocation only, with unchanged values, counts, and order.
- Each template's placements occupy one contiguous run, and every run's mesh-visible placements precede its offscreen placements, because one indirect command draws `instanceCount` from `firstInstance` while the prepasses draw the whole arena.
- Per-framebuffer instances are written only for the acquired framebuffer index, preserving the existing frames-in-flight write-timing rule; no new synchronization is introduced.
- Global and Main command buffers stay recorded once: every value baked into them (`miTemplateCount`, `kiMaxActivePlacements`) remains boot-fixed.
- Arena entries above the current written total are cleared against the previous written total, so no stale placement is drawn by either prepass.
- Allocation tracking stays clean: SSBO and indirect allocation remain boot-only, and per-frame counting/prefix-sum use `gpThreadLocal->mWorkbuffer`.
- No serialization, `.pack`, `kiVersion`, replay, wire-format, shader-source, or descriptor-layout change is permitted.

## Acceptance criteria

- Client and server targets compile. `kiMaxActivePlacements` evaluates to 1819 from `kiCoordSlots` 16 and `kiMaxIslandsPerCell` 107, and is derived in source rather than written as a literal.
- An existing `/agent-harness` normal connected-gameplay scenario reaches island-terrain rendering, produces a terrain screenshot visually equivalent to the pre-change baseline capture at the same deterministic state, and logs no Vulkan validation error and no placement-capacity assertion. Island count, placement positions, and rotations are unchanged on screen.
- Moving the camera across enough cell boundaries to cycle the subscribed set produces no flicker, no missing or duplicated islands, and no stale islands from a previously active cell — the observable proof that per-frame offsets and the tail clear are correct.
- The placement storage buffers are allocated at `kiMaxActivePlacements` entries and no longer multiply by `miTemplateCount`, confirmed by source inspection of the allocation site: 436,560 bytes across the four framebuffer instances, independent of template count, rather than 17,203,200 bytes at the captured 70-template set.
- Command buffers are still recorded once: no re-record occurs outside swapchain recreation, and no per-frame heap allocation is reported by allocation tracking during steady-state rendering.
- Server deterministic placement behavior is unchanged by source inspection and the shared target build.

## Notes

- This Plan supersedes the earlier direction of raising the fixed per-template slab from 1024 to a larger fixed literal. That direction was rejected because the per-template shape is the source of the problem: it reserves capacity per template that the global bound proves can never be used, and any fixed per-template literal either wastes memory or asserts a cell-count contract the renderer does not enforce.
- The bound is derived, not asserted, deliberately. A `static_assert` against a literal would fail the build on a legitimate contract change; a derived arena size resizes correctly and stays cheap — even at the `shaders::kiMaxIslands` cap the arena is well under 1 MiB.
- `Documents/Investigations/Graphics/IslandResidentMemoryScaling_Overview.md` records that the fixed arena was retained on memory grounds alone, as the smallest of five residency buckets. It contains no analysis rejecting compaction. The prepass instance-count reduction, not the memory saving, is the stronger argument here.
