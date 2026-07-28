<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T02:05:35.456Z","dependsOn":[]} -->
# Window lighting deposit, spread, combine, and temporal work without command-buffer rerecording

## Context

The approved shadow-only split has completed and been removed from
`Documents/Plans/`; this plan's metadata `dependsOn` is empty and it has no live prerequisite. This plan is the
recorded follow-up debt from that split: lighting deposit, the spread chain, combine, and temporal history still
pay full-texture cost.

Current state, grounded in the code:

- `CommandBufferRecordMain::RecordLightingDeposit` (`Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`
  ~line 67) records the deposit MRT render pass over the full `mpLightingTextures[0]` extent, and
  `RenderTargetTextures::CreateLightingTextures` (`RenderTargetTexturesLighting.cpp` ~line 96) creates all three
  deposit attachments with `VK_ATTACHMENT_LOAD_OP_CLEAR`.
- `CommandBufferRecordMain::RecordLightingSpreadPipeline` (~line 109) records every spread pass over its full
  per-pass extent with all six spread MRT attachments `VK_ATTACHMENT_LOAD_OP_CLEAR`
  (`RenderTargetTexturesLighting.cpp` ~line 223), then dispatches `mCombinePipeline` and
  `mLightingTemporalPipeline` with fixed `TileCount(full combine extent)` group counts, and refreshes history by
  recorded full-image `vkCmdCopyImage` transfers (`RecordCopyImageFrom`, ~lines 222-235).
- `LightingSpread.frag`, `LightCombine.comp`, and `LightingTemporal.comp` already identify a visible window
  (`f2SpreadMargin` / `f2CombineMargin` early-outs), but only after the full graphics or compute footprint is
  launched.
- `PopulateLightingParameters` (`Engine/Source/Graphics/Render/LightingUniforms.cpp`) latches only
  `f4LightingArea`/`f4LightingAreaPrevious` through `TemporalAreaLatch`; there are no explicit valid subwindow
  bounds in `shaders::GlobalLayout` (`Engine/Data/Shaders/ShaderLayoutsBase.h` ~lines 337-340, 378).

Dynamic scissor cannot repair this while preserving record-once command buffers: recorded state cannot vary per
frame, and it does not restrict render-pass `LOAD_OP_CLEAR`, which clears the recorded full `renderArea`. A
one-pass spread margin is also insufficient for a `shaders::kiMaxSpreadPasses` (40-pass) dependency chain.
Temporal reprojection currently has only the area previous latch; once data outside a subwindow can remain stale,
it also needs explicit current/previous valid subwindow bounds and consumer protections for filtered samples.

## Risk tier

Tier 3: client-only CPU/GLSL/Vulkan integration crossing command recording, render-pass/load semantics,
descriptor/layout synchronization, history ownership, and Terrain/Water sampling. No simulation CRC, wire,
save/replay, `.pack`, or server affinity exposure.

## Scope contract

The listed scope is both target and ceiling. Make the smallest complete change satisfying the design and
acceptance criteria; add no abstractions, configuration, refactors, or fixes to adjacent code encountered along
the way. Naming a file grants permission only for the named functions, members, or regions plus the mechanical
necessities the named change requires (includes, declarations, project membership).

### In scope (file — named regions)

- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — `RecordLightingDeposit` (bounded compute clear
  before the deposit render pass; render pass switched to load semantics) and `RecordLightingSpreadPipeline`
  (bounded spread geometry draws; indirect combine/temporal dispatch; compute history copy replacing the
  `RecordCopyImageFrom` transfer block; the exact barriers and layout transitions these require).
  `CommandBufferRecordMain.h` only for any new private record helper declaration.
- `Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp` — `CreateLightingTextures` only: deposit
  attachment `loadOp` CLEAR->LOAD and `VK_IMAGE_USAGE_STORAGE_BIT` on `mpLightingTextures` (for the compute
  clear), spread attachment `loadOp` CLEAR->LOAD, and history-copy usage/comment corrections; the persistent
  history bank (`mpLightingHistoryTextures`, `mAmbientHistoryTexture`) and `gbLightingTemporalReset` re-arm stay
  as-is. `RenderTargetTextures.h` only if a declaration change is mechanically required.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `CreateLightingPipelines` only: new bounded-clear and
  history-copy compute pipelines (mirroring the `kPipelineShadowHistoryCopy` creation pattern in
  `CreatePipelineShadows`), `mSpreadPipelines` vertex-shader swap to the new windowed quad shader, and
  combine/temporal pipeline indirect-dispatch plumbing. `PipelineManager.h` only for the new pipeline
  enum/member declarations.
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` — `PopulateLightingParameters`, `RenderLightingGlobal`,
  and `RenderLightingSpreadIndirect`: rectangle derivation, backward spread closure, refresh predicate, indirect
  command writes, current/previous valid-bounds publication, and latch ordering.
  `Engine/Source/Graphics/Render/Render.h` only for the lighting globals/`TemporalAreaLatch` region (~lines
  55-101) declarations this requires.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `GlobalLayout` lighting region only: current valid rectangle,
  prior-history valid rectangle, per-pass spread bounds fields; any indirect-dispatch push-constant layout.
- `Engine/Data/Shaders/Lighting/AreaLight.frag`, `PointLight.frag`, and
  `Engine/Data/Shaders/Objects/HexShieldLighting.frag` — add the early half-open `gl_FragCoord` rectangle
  discard only; do not clamp or reshape the projected geometry.
- `Engine/Data/Shaders/Lighting/LightingSpread.frag` — windowed-rectangle rework of the existing
  `f2SpreadMargin` window block (~lines 78-99), including removal of the obsolete single-margin carry-forward
  branch.
- `Engine/Data/Shaders/Lighting/LightCombine.comp` and `LightingTemporal.comp` — absolute-min-texel indirect
  coordinates and valid-rectangle/history-validity guards replacing the `f2CombineMargin` window tests.
- New shaders: `Engine/Data/Shaders/Lighting/LightingClear.comp`,
  `Engine/Data/Shaders/Lighting/LightingHistoryCopy.comp`, and
  `Engine/Data/Shaders/Quads/QuadsLightingWindowed.vert`.
- `Engine/Data/Shaders/Terrain/Terrain.frag` (lighting sample site, `pLightingSamplers` region ~line 118),
  `Engine/Data/Shaders/Water/Water.frag` (direct, base-height, reflected, and ambient lighting sample paths,
  ~lines 470-545), and `Engine/Data/Shaders/ShaderFunctions.h` (`ReadLighting` and the lighting helpers it
  feeds) — linear-footprint valid-window guards.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `.filters` — new
  shader membership and client-only affinity verification.

### Out of scope

- Lighting refresh cadence, slider behavior, and its intentional skipped-frame visual tradeoff — owned by
  `Documents/Plans/Graphics/LightingRefreshCadence.md`; this plan only establishes the predicate hook.
- Object shadows and all shadow dispatch/blur/temporal work (already landed by the shadow-only split).
- Spread-kernel quality/cost tuning, texel-ramp policy, and changing full lighting texture allocation.
- Simulation, determinism/CRC, networking, save/replay compatibility, asset packing, and server code.
- Any command-buffer rerecording path or dynamic-scissor mechanism.

## Design

- Preserve record-once command buffers. Per-frame window variation flows through uniform data, indirect
  compute commands, and shader-generated bounded graphics geometry; do not add command-buffer rerecording or
  rely on dynamic scissor.
- Derive clamped integer texel rectangles from the current visible area for every lighting texture extent.
  Use floor/ceil coverage, guard zero and out-of-bounds dimensions, and preserve the full headroom allocation.
  Carry the current valid rectangle and the prior-history valid rectangle explicitly in `shaders::GlobalLayout`
  rather than inferring validity from area alone.
- Add `LightingClear.comp` to zero the three deposit storage images (`mpLightingTextures`) only over the active
  rectangle before the deposit render pass, then change the deposit render pass to `LOAD`. Area/point world
  quads and HexShield meshes already project localized geometry, so do not clamp or distort them: add an early
  half-open `gl_FragCoord` rectangle discard to `AreaLight.frag`, `PointLight.frag`, and
  `Objects/HexShieldLighting.frag` so no deposit write escapes the cleared rectangle. This bounds writes, not
  their existing localized raster cost.
- Compute spread rectangles by walking backward from the final consumer-visible rectangle through every enabled
  spread pass. Include each pass's interpolated spread distance, jitter reach, linear-filter footprint, and its
  mixed source/destination extent when expanding the successor requirement. Clamp each result to that pass's
  extent. New `QuadsLightingWindowed.vert` replaces `QuadsFullscreen.vert` for `mSpreadPipelines` only: the
  existing fixed push constants (`.xy` = pass extent, `.z` = pass index, already recorded by
  `RecordLightingSpreadPipeline`) index the per-pass GlobalLayout bounds; it maps quad UV edges to the clip
  rectangle while retaining full-texture source UVs. All six spread MRT attachments change to `LOAD`, and every
  refresh draws the bounded pass rectangle even when deposit is empty. Remove the obsolete single-margin
  carry-forward branch in `LightingSpread.frag`. This full-chain closure guarantees every in-window gather reads
  a freshly written predecessor while stale data outside the rectangle remains loaded but invalid.
- Convert combine and temporal to indirect window dispatches (`Pipeline::WriteIndirectComputeBuffer` /
  `RecordComputeIndirect`, the existing `kPipelineWaterDisplacement` and shadow-window pattern) with an
  absolute-min-texel offset and in-shader bounds guards. They process their required valid rectangles rather
  than the full combine extent. Retain defensive bounds checks for indirect-command, ceil-to-tile, and recreate
  edge cases.
- Keep the one persistent four-image history bank (`mpLightingHistoryTextures[3]` + `mAmbientHistoryTexture`)
  with static descriptors; do not rotate it. `LightingTemporal.comp` samples that bank and writes only combine.
  New `LightingHistoryCopy.comp` then reads the four combine storage images and writes the four distinct history
  storage images only over the current valid rectangle, replacing the recorded full-image `vkCmdCopyImage`
  transfers. Record the corresponding layout transitions and barriers in the same ordering as the shadow history
  copy (`kPipelineShadowHistoryCopy` / `ShadowHistoryCopy.comp`), so temporal never samples and writes the same
  image and the next history publication is synchronized.
- Establish the cadence refresh predicate in `RenderLightingGlobal` before `PopulateLightingParameters` and the
  `TemporalAreaLatch` area/valid-bounds latch. On a refresh, advance and publish `{area, validBounds}` together,
  then refresh exactly that history rectangle after temporal; on a cadence skip, deposit clear and localized
  raster continue every frame, but advance neither area, bounds, nor history; suppress spread through its
  indirect draw count; and write zero groups for combine, temporal, and history copy. The next refresh clears
  and rebuilds the active chain, intentionally leaving skip-frame deposits unconsumed/dropped. Recreate
  (`gbLightingTemporalReset`) invalidates the latch and forces first refresh, blend `1`, and no history fetch.
  The later deposit-count writer consumes this established predicate rather than making the latch decision
  solely in `RenderLightingSpreadIndirect` (`giLightingDepositInstances` gate).
- Update every lighting consumer to reject a sample unless its full linear-filter footprint lies inside the
  current valid rectangle. Terrain and Water must return the established no-light result for invalid direct,
  base-height, ambient, and reflected sample paths, preventing stale in-texture data from leaking after pan,
  zoom, resize, or recreate.

## Acceptance criteria

- At settled camera height, GPU captures show bounded deposit clear, full spread chain, combine, temporal, and
  history-copy work scale with their bounded rectangles rather than the full allocation; existing lighting
  timers (`kGpuTimerLightingDeposit`/`Spread`/`Combine`/`Temporal`) and active-pixel diagnostics
  (`giLightingDepositPixels*`, `giLightingSpread*ActivePixels*`) report the covered footprint. Do not claim that
  the already-localized deposit quad/mesh raster cost itself scales with the rectangle.
- A settled lit scene is visually equivalent to the pre-windowed lighting result inside the visible area;
  capture screenshots and lighting render-target dumps before/after to compare the result.
- Pan and zoom in both directions, including rapid zoom-out and return to steady state, show no edge seam,
  stale-light leak, missing gather contribution, or temporal smear. Exercise all Terrain and Water lighting
  paths, including reflected/base-height and ambient samples.
- Resize, pipeline recreation, and history recreation reset or publish valid bounds correctly; the first frame
  never samples undefined or stale history.
- Vulkan validation remains clean for deposit/six-spread `LOAD`, indirect dispatch, static descriptor usage,
  image layouts, shadow-pattern history barriers, and no same-image temporal alias. Agent-harness runs cover
  settled, pan/zoom, resize, recreate, and cadence-skip scenarios with GPU timing/log evidence.
- Client build passes. No command-buffer rerecord is introduced, and full texture allocation remains unchanged.

## Coordination

- `Documents/Plans/Graphics/LightingRefreshCadence.md`: never interleave. Both plans own
  `CommandBufferRecordMain` combine/temporal recording and `LightingUniforms` population. Co-schedule one
  session or land sequentially; the later plan refreshes shared-site citations and extends the first change
  instead of duplicating its indirect-dispatch conversion.

## Notes

- Existing host-visible indirect plumbing (`Pipeline::WriteIndirectBuffer`, `WriteIndirectComputeBuffer`,
  `RecordComputeIndirect`) is already present; this plan wires the lighting-specific rectangles and passes
  rather than recreating that helper.
- The zero-instance spread gate's current correctness comment (a skipped draw equals a gather over an all-zero
  cleared deposit) is invalidated by the CLEAR->LOAD switch; the refresh predicate and always-drawn bounded
  refresh above are the replacement contract — update those comments with the change.
- This plan records approved out-of-scope debt from the completed shadow-only split, not an acceptance failure
  of that implementation.
