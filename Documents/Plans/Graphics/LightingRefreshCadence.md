<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-21T23:23:30.000Z","dependsOn":[]} -->
# Slider-gated refresh cadence for the lighting spread → combine → temporal chain

## Context

The lighting spread empty-deposit skip shipped and its plan was completed and removed from the queue: the
40 lighting spread draws are now host-visible indirect draws, and `engine::RenderLightingSpreadIndirect` writes
`instanceCount = 0` on frames whose light deposit is empty. Measured effect (Debug, 1600×904): idle
`kGpuTimerLightingSpread` **645 µs → 84–92 µs**, idle spread render targets byte-identical (SHA-256) to
the pre-change build, Vulkan validation clean across three pipeline recreates and two resizes.

That win is **content gating** and is confined to the idle case: the moment any light deposits, spread
returns to full cost. Item 2 of the completed plan — **rate gating** — was never implemented and is the
residual this plan owns. It refreshes the spread → combine → temporal chain every N frames and freezes
the combine textures at the last refreshed lighting on skip frames. Unlike item 1, it also pays off in
combat, where the deposit is never empty.

### Mechanism already in place (verified this session)

The gate item 1 built is the foundation:

- `Engine/Source/Graphics/Render/Render.h:98` — `inline int64_t giLightingDepositInstances = 0;` (reset at
  the top of `RenderFrameMain`, accumulated by the three deposit `EndRender` writers).
- `Engine/Source/Graphics/Render/Render.h:101` — `void RenderLightingSpreadIndirect(int64_t iCommandBuffer);`,
  implemented in `LightingUniforms.cpp`.
- `Engine/Source/Graphics/Render/MainUniforms.cpp:483` **and `:553`** — **two** call sites, each immediately
  after `game::FrameInterpolate::EndRender(iCommandBuffer)`.

### Correction to the completed plan's stated prerequisites

The completed plan claimed item 2 first required the `kIndirectHostVisible | kCompute` branch in
`PipelineCreator.cpp` plus `WriteIndirectComputeBuffer` / `RecordComputeIndirect` to be written. **All three
already ship**, verified against current source:

- `Engine/Source/Graphics/Objects/Pipeline.cpp:334` — `void Pipeline::RecordComputeIndirect(int64_t iCommandBuffer, VkCommandBuffer vkCommandBuffer, const XMFLOAT4& f4PushConstants)`
- `Engine/Source/Graphics/Objects/Pipeline.cpp:386` — `void Pipeline::WriteIndirectComputeBuffer(int64_t iCommandBuffer, int64_t iGroupCountX, int64_t iGroupCountY, int64_t iGroupCountZ)`
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp:685` — real per-framebuffer host-visible
  `VkDispatchIndirectCommand` allocation (**not** `ASSERT(false)`)
- Live consumer precedent: `kPipelineWaterDisplacement` sets `.flags = {kCompute, kIndirectHostVisible}`
  (`PipelineManager.cpp:448`, `Create` call at `:445`) and writes its group counts at
  `MainUniforms.cpp:517`.

So this plan only has to **convert the two lighting dispatches** and wire the cadence — no plumbing work.

## Design

Add a frame counter and a refresh predicate; on skip frames suppress the spread → combine → temporal → history
copy chain and hold the published combine/history data.

### Refresh gate

- **Skip frame**: write zero indirect groups for combine, temporal, and (after
  `WindowedLightingDispatch.md`) history copy; do not advance the temporal area/valid-bounds latch or history.
  Deposit, including bounded clear and localized raster after windowing, continues every frame. Suppress only
  spread, combine, temporal, and history copy; the combine textures hold the prior refresh's output. The next
  refresh clears and rebuilds the active chain, so deposits made during skip frames are intentionally
  unconsumed/dropped.
- **Refresh frame**: everything normal.
- Before the windowed plan lands, cadence 1 (default) takes the item-1 path unchanged — including its
  empty-deposit gating — so the default build is bit-identical to what just landed. Once the windowed plan
  lands, cadence 1 still performs every refresh, but a refresh runs every bounded spread pass even with empty
  deposit so its current rectangle is overwritten.

Establish the cadence predicate in `RenderLightingGlobal` before `PopulateLightingParameters`; that lets the
area/valid-bounds latch make the same decision before its temporal state is populated. Cache the predicate for
the later `RenderLightingSpreadIndirect` calls after `FrameInterpolate::EndRender`, where the deposit count is
available and the per-pass commands are written. This keeps both `MainUniforms.cpp:483` and `:553` aligned
without making the later function the sole latch owner.

### Dispatch conversion

Convert the two direct dispatches inside `CommandBufferRecordMain::RecordLightingSpreadPipeline`
(defined at `CommandBufferRecordMain.cpp:109`):

- Phase 2 combine — `CommandBufferRecordMain.cpp:193`:
  `vkCmdDispatch(vkCommandBuffer, uiCombineGroupsX, uiCombineGroupsY, 1);`
- Phase 3 temporal — `CommandBufferRecordMain.cpp:219`:
  `gpPipelineManager->mLightingTemporalPipeline.RecordCompute(iCommandBuffer, vkCommandBuffer, uiTemporalGroupsX, uiTemporalGroupsY);`

Both become `RecordComputeIndirect`, with their pipelines gaining `kIndirectHostVisible` alongside
`kCompute`, following the `kPipelineWaterDisplacement` precedent exactly. The group counts these calls
currently compute inline move to the populate path as `WriteIndirectComputeBuffer` arguments.

### Temporal latch and history

`{f4LightingArea, validBounds}` must advance **only on refresh frames**, so reprojection maps into the area and
subwindow history actually rendered with (matters while the texel ramp rescales during a zoom). The latch is
established before `PopulateLightingParameters`, not later in `RenderLightingSpreadIndirect`. Recreate
invalidates it, forces the first refresh to blend `1`, and prevents a history fetch. After the windowed plan
lands, its `LightingHistoryCopy.comp` updates the single persistent history bank only on refresh; cadence skip
writes zero history-copy groups and advances no history state.

```
rGlobalLayout.fLightingTemporalBlend = sTemporalAreaLatch.Update(rGlobalLayout.f4LightingArea, gbLightingTemporalReset, gLightingTemporalBlend.Get(), rGlobalLayout.f4LightingAreaPrevious);
```

Replace this area-only update with the shared area/valid-bounds refresh decision; keep predicate establishment
and latch advance in the same early location.

### Slider

New `gLightingUpdateCadence` — **range 1–4, snap step 1, default 1 = off**.

Note the actual repository pattern (the completed plan's phrasing was loose): `LightingWrappersBase.h`
holds bare `extern Wrapper gName;` declarations only; the range and step are constructor arguments in
`LightingWrappersBase.cpp`. No lighting wrapper currently passes an explicit step — use the 4-arg form
already used by shadow/water, e.g. `ShadowWrappersBase.cpp:30`:

```
Wrapper gObjectShadowsBlurRadius(8.0f, 1.0f, 32.0f, 1.0f);
```

Expose it through the existing `TweaksSliderMap` in
`Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp`, matching the entry form at `:17`:

```
{"Lighting Blur Sigma", &gLightingBlurSigma},
```

Deposit keeps running every frame (cheap); deposits on skip frames are unconsumed, so a light flash
shorter than the cadence is dropped.

### Expected saving

Roughly (spread + combine + temporal) / 2 at cadence 2. Post-item-1 the idle residual is small
(spread 84–92 µs + combine + temporal), but in combat spread returns to full cost, which is where this
plan's value is — quantify with an A/B in a lit combat scene, not idle.

## Critical files

- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — `RenderLightingGlobal` establishes the cadence
  predicate before `PopulateLightingParameters` and area/valid-bounds latch; the later
  `RenderLightingSpreadIndirect` consumes it after deposit accumulation and writes command groups.
- `Engine/Source/Graphics/Render/Render.h` — any shared cadence state alongside `giLightingDepositInstances`
  (`:98`) and `RenderLightingSpreadIndirect` (`:101`); `TemporalAreaLatch` at `:55`.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — `RecordLightingSpreadPipeline` (`:109`):
  combine dispatch `:193` and temporal `RecordCompute` `:219` → indirect; after windowing, also records the
  bounded history-copy stage.
  **Shared with `WindowedLightingDispatch.md`.**
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — combine + temporal compute pipelines gain
  `kIndirectHostVisible` (pattern: `:445-448`); after windowing, history copy participates in the same cached
  refresh predicate.
- `Engine/Source/Graphics/Objects/Pipeline.cpp` — existing `RecordComputeIndirect` (`:334`) /
  `WriteIndirectComputeBuffer` (`:386`); no change expected.
- `Engine/Source/Ui/LightingWrappersBase.h` (extern decl) and `.cpp` (4-arg constructor with range/step) —
  `gLightingUpdateCadence`.
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenLighting.cpp` — slider map entry.
- `Engine/Source/Graphics/Render/MainUniforms.cpp:483`, `:553` — the two `RenderLightingSpreadIndirect`
  call sites (read-only unless the gate is placed at the call sites instead).

## Out of scope

- The completed plan's empty-deposit spread gate — before windowing this plan reuses it; windowed `LOAD`
  passes supersede it with bounded work on every refresh.
- Shadow-pass dispatch belongs to `WindowedLightingShadowDispatch.md`. `WindowedLightingDispatch.md` owns
  lighting footprint windowing, bounded `LOAD` deposit/spread, lighting min-texel offsets, and bounded history
  copy.
- Spread kernel quality/cost tuning (pass, ring, direction counts, texture multipliers) — already sliders.
- The deposit pass, terrain shadows, and object shadows.
- Cadence for any pass outside the spread → combine → temporal chain.

## Acceptance criteria

- Cadence 1 (default): output bit-identical to the applicable preceding lighting implementation — same idle
  behavior and, before windowing, same empty-deposit spread skip.
- Cadence 2 in a **lit combat scene**: `kGpuTimerLightingSpread` + `kGpuTimerLightingCombine` +
  `kGpuTimerLightingTemporal` average roughly halves.
- Cadence 2: no visible glow stutter panning over a static lit scene; capture the glow-lag and
  dropped-flash behavior at cadence 3–4 so the default can be revisited on evidence.
- Zoom with cadence > 1: no reprojection smear — the previous area and valid bounds advance only on refresh
  frames, and history-copy work is skipped with the chain.
- No command-buffer re-record introduced; Vulkan validation clean across pipeline recreate and resize
  (indirect-dispatch buffer usage, zero group counts).

## Coordination

- `Documents/Plans/Graphics/WindowedLightingDispatch.md`: **never interleave.** Both plans convert the same
  combine/temporal recording and edit the same `LightingUniforms.cpp` populate region. Co-schedule in one
  session, or land sequentially; the later plan refreshes shared-site citations and extends the first change
  instead of duplicating its indirect-dispatch conversion. The windowed plan's shadow prerequisite does not
  make cadence a prerequisite; either sequential order remains valid under the contracts above.

## Notes

- **Invariant exposure**: client/graphics-only render path. No shader edits and no repack (group counts are
  CPU-side). No determinism/CRC, wire, `kiVersion`, `.pack`, or replay exposure.
- `LightingUniforms.cpp` is allocation-tracked main-loop code — the cadence path must stay heap-free
  (fixed loops, no containers).
- Zero group counts are valid Vulkan (`vkCmdDispatchIndirect` with a zero dimension launches no
  workgroups); confirm validation stays clean rather than assuming it.
- This is the only visual-risk item from the original plan, which is why it ships default-off (cadence 1)
  until A/B'd. Half-rate refresh hidden behind an existing temporal-accumulation pass is standard practice;
  the EMA blend in `LightingTemporal.comp` is the compensator.
