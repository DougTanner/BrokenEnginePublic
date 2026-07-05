# Gate the lighting spread chain: zero-instance skip when the deposit is empty, optional half-rate cadence

## Context

GPU profile (Profile build, 120 fps, idle scene, ~6.6 ms total GPU/frame): `kGpuTimerLightingSpread` **841 µs** (avg 823, max 860) — the largest single lighting cost — plus `kGpuTimerLightingCombine` 24 µs and `kGpuTimerLightingTemporal` 18 µs. The idle scene has few/no dynamic lights, yet spread pays full price: the cost is **content-independent**.

Cost anatomy (why 841 µs despite tiny textures): the spread textures are small — per-pass size interpolates `gSpreadTextureMultiplierStart` (0.05) → `gSpreadTextureMultiplierEnd` (0.01) through `TextureManager::LightingDetailTextureSize` (≈288×162 down to the 128×64-min clamp ×1.5 headroom at a 3840-wide framebuffer). The cost is the **in-window radial gather**: at defaults (`gSpreadRingCount` 4→3, `gSpreadDirectionCount` 4) each in-window fragment does ~22 sample points × 3 channel textures ≈ 66 bilinear float16 fetches, ~44% of ~47K texels per pass are in-window, × `gSpreadPassCount` (40) serialized passes ≈ **~55M dependent texture fetches per frame**, plus 40× render-pass begin/end + inter-pass barriers. This gather runs at full cost even when every fetched texel is zero (no lights deposited).

**Relationship to `Graphics/WindowedLightingShadowDispatch.md`** (live, overlapping): that plan windows the *footprint* — indirect compute dispatch for combine/temporal/shadow and a dynamic scissor for deposit/spread — removing the off-window passthrough work. For spread specifically the off-window fragments are already a cheap 3-fetch passthrough via the in-shader early-out, so the scissor recovers only ~5–10% of the 841 µs; the in-window gather that dominates is untouched by windowing. This plan adds the orthogonal mechanism: **content gating** (don't run the gather at all when there is nothing to gather) and optionally **rate gating** (refresh at half rate). Both plans edit `CommandBufferRecordMain::RecordLightingSpreadPipeline` and `LightingUniforms.cpp` — **co-schedule in one session or land sequentially with citation refresh; never interleave**. The cadence item reuses the combine/temporal `vkCmdDispatchIndirect` conversion that plan introduces (whichever lands first implements it).

Also live nearby: `Graphics/Architecture_ShadowLightingUniformDedup.md` (extracts the temporal-area latch this plan's cadence item gates — resolve latch-gating placement together if co-scheduled).

## Design

### Item 1 — skip spread when the frame deposits no light (no visual change)

Convert the 40 spread fullscreen draws from `RecordDraw(..., 1, 0, {w, h, pass})` to host-visible indirect draws, then write `instanceCount = 0` on frames whose light deposit is empty:

- `PipelineManager::CreateLightingPipelines`: add `kIndirectHostVisible` to the `mSpreadPipelines` `PipelineInfo` flags (same flag/plumbing the deposit `DynamicPipelines` already use; each pipeline gets its per-framebuffer `VkDrawIndirectCommand` map).
- `CommandBufferRecordMain::RecordLightingSpreadPipeline`: `RecordDraw` → `RecordDrawIndirect` (push constants `{width, height, passIndex}` unchanged — `RecordDrawIndirect` takes the same `XMFLOAT4`). Render-pass begin/end, clears, and barriers stay recorded unconditionally.
- Per-frame, from the lighting populate path: emptiness = the three deposit pipeline families (`kDynamicPipelineLighting`, `kDynamicPipelineAxisAlignedLighting`, `kDynamicPipelineHexShieldsLighting`) all wrote instance count 0 for this command buffer (`AreaLightsRender`/`PointLightsRender`/hex-shield render each call `WriteIndirectBuffer(iCommandBuffer, siRendered)`). Write `WriteIndirectBuffer(iCommandBuffer, bEmpty ? 0 : 1)` for all 40 spread pipelines.

Correctness: with zero instances the spread render passes still run their `LOAD_OP_CLEAR`, so every spread + spread-only texture is zero. `LightCombine` (unchanged, still runs) sums zeros and tone-maps to zero (`Uchimura(0)` = `gCombineBlackTightness` = 0), and `LightingTemporal` EMA-fades the on-screen result — **bit-identical to actually running the gather over an empty deposit** (zero in → `0 / fNorm` → zero out, same single-frame zero + temporal fade). No hysteresis needed: a light appearing writes `instanceCount = 1` the same frame it deposits.

Expected saving: idle scene ~**650–750 µs** of the 841 (residual = 40 render-pass begin/ends + fast clears + barriers). Combat frames with any live light are unchanged.

**Visual impact: (a) no visual change** — output is bit-identical on both edges of the transition. No slider needed.

### Item 2 — optional refresh cadence (slider-gated, default off)

Refresh the spread→combine→temporal chain every N frames; skip frames freeze the combine textures at the last refreshed lighting:

- Skip frame: spread `instanceCount = 0` (as item 1) **and** combine + temporal indirect dispatch group counts = 0 (requires the `vkCmdDispatchIndirect` conversion from `WindowedLightingShadowDispatch`; if this plan lands first, convert those two dispatches here the same way). Combine textures then hold the previous refresh's output (the spread clears wipe textures nobody reads that frame); the recorded history copies re-copy unchanged data (benign) — or, if the windowing plan's temporal-writes-history option landed, history is simply not rewritten (also correct).
- Refresh frame: everything normal. The `f4LightingAreaPrevious` temporal latch must be advanced only on refresh frames so reprojection maps into the area the history was actually rendered with (matters only while the texel ramp is rescaling during a zoom; interacts with `Architecture_ShadowLightingUniformDedup`'s `TemporalAreaLatch` extraction — keep the gate in one place).
- New wrapper `gLightingUpdateCadence` (`LightingWrappersBase`, snap step 1, range 1–4, **default 1 = off**) + `TweaksScreenLighting` slider "Update Cadence" via the existing `TweaksSliderMap` pattern. Deposit keeps running every frame (cheap, 42 µs incl. clear); deposits on skip frames are unconsumed — a light flash shorter than the cadence is dropped (at 120 fps / cadence 2 that is a sub-8 ms flash; unlikely visible, but the reason default stays 1 until A/B'd).

Expected saving at cadence 2: ~**(841 + 24 + 18) / 2 ≈ 440 µs average** — and unlike item 1 it also pays off in combat.

**Visual impact: (b) minor** — lighting glow updates at 60 Hz under a 120 fps present; the glow field is low-frequency and the temporal EMA already smooths refreshes. Exposed as the runtime slider above (range 1–4, default 1); worst case (fast-moving bright light) shows slight glow lag at cadence ≥ 3.

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `CreateLightingPipelines`: `mSpreadPipelines[i]` `PipelineInfo::flags` gains `kIndirectHostVisible`.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — `RecordLightingSpreadPipeline`: spread `RecordDraw` → `RecordDrawIndirect`; (item 2) combine/temporal dispatch → `RecordComputeIndirect`/`vkCmdDispatchIndirect` if not already landed by the windowing plan. **Shared with `WindowedLightingShadowDispatch.md`.**
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — `PopulateLightingParameters`: read the three deposit families' written instance counts, write the 40 spread `WriteIndirectBuffer` values (+ item 2: refresh-bit multiply on the combine/temporal group counts, gate the previous-area latch).
- `Engine/Source/Graphics/Objects/Pipeline.h/.cpp` — existing `WriteIndirectBuffer` / `RecordDrawIndirect` (no change expected; verify the non-indexed fullscreen-triangle path writes `vertexCount = 3`).
- `Engine/Source/Frame/Collections/AreaLights/AreaLightsRender.cpp`, `PointLights/PointLightsRender.cpp`, hex-shield lighting render — the `WriteIndirectBuffer(iCommandBuffer, siRendered)` emptiness sources (read-only, or gain a shared flag per the grill decision).
- Item 2 only: `Engine/Source/Ui/LightingWrappersBase.{h,cpp}`, `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` — cadence wrapper + slider.

## Out of scope

- Spread kernel quality/cost tuning — pass count, ring/direction counts, texture multipliers are already runtime sliders (`gSpreadPassCount`, `gSpreadRingCount*`, `gSpreadTextureMultiplier*`); no algorithmic kernel change (e.g. dual-Kawase pyramid replacement) — deliberately rejected as a redesign of an artistically tuned system.
- Footprint windowing / scissor / min-texel offsets — `WindowedLightingShadowDispatch.md`.
- The deposit pass itself (the occupancy clear/atomics have already been removed; remaining deposit cost is unrelated to this plan).
- Terrain shadow chain and object shadows.
- Skipping the recorded render-pass begin/end + clears on empty frames (would need CB re-record — banned).

## Acceptance criteria

- Idle scene with zero live lights: `kGpuTimerLightingSpread` drops from ~841 µs to render-pass overhead (expect < ~150 µs); timers return to normal the frame a light spawns.
- Light spawn/despawn transitions look identical to current (bit-identical combine output on both edges).
- Item 2 at cadence 2: no visible glow stutter panning over a static lit scene; `Spread`+`Combine`+`Temporal` average halves.
- No CB re-record introduced; Vulkan validation clean (indirect draw buffer usage).

## Notes

- **Invariant exposure**: client/graphics-only render path. Item 1 requires **no shader edits and no repack** (C++ only). Item 2 as scoped also needs no shader edit (group counts are CPU-side; the min-texel offset shader work belongs to the windowing plan). No determinism/CRC/wire/`kiVersion`/replay exposure. `LightingUniforms.cpp` is allocation-tracked main-loop code — no heap allocation in the new per-frame path (fixed loops over the 40 pipelines and 3 pipeline maps).
- **Grill decisions to pre-stage**: (1) emptiness source — read the deposit pipelines' just-written host-visible `VkDrawIndirectCommand` instance counts at populate time (verify collection render population precedes `PopulateLightingParameters` in the frame; it does today) vs. a shared per-frame flag set by the three writers; (2) ship item 2 at all, and its default (1 vs 2); (3) sequencing vs `WindowedLightingShadowDispatch` (co-schedule recommended — shared functions and the shared indirect-dispatch conversion).
- Half-rate refresh hidden by an existing temporal-accumulation pass is standard practice (temporal reprojection / reduced-rate effect updates); the EMA blend already in `LightingTemporal.comp` is the compensator.
