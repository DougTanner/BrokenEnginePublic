<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T02:05:35.456Z","dependsOn":[]} -->
# Window lighting deposit, spread, combine, and temporal work without command-buffer rerecording

## Context

`WindowedLightingShadowDispatch.md` was approved to split shadow work from lighting work. The active,
claimed plan now owns only shadow dispatch; it is immutable until its completion sidecar removes it. Its
completion is this plan's directional prerequisite. Lighting deposit, the 40-pass spread chain, combine, and
temporal history remain untouched and are explicitly this plan's follow-up debt.

The current lighting path still pays a full-texture cost even where its shaders recognize the live visible
window. `CommandBufferRecordMain::RecordLightingDeposit` records a full render area, and
`RenderTargetTexturesLighting.cpp` creates its deposit and six-attachment spread render-pass attachments
with `VK_ATTACHMENT_LOAD_OP_CLEAR`. `RecordLightingSpreadPipeline` records all spread passes over their full
extent, dispatches combine and temporal over the full combine extent, then copies all four combine images to
history. `LightingSpread.frag`, `LightCombine.comp`, and `LightingTemporal.comp` already identify a visible
window, but their early-outs occur only after the full graphics or compute footprint is launched.

Dynamic scissor cannot repair this while preserving record-once command buffers: its recorded state cannot
vary per frame, and it does not restrict render-pass `LOAD_OP_CLEAR`, which clears the recorded full
`renderArea`. A one-pass spread margin is also insufficient for a 40-pass dependency chain. Temporal
reprojection currently has only an area previous latch; once data outside a subwindow can remain stale, it
also needs explicit current/previous valid subwindow bounds and consumer protections for filtered samples.

This is a Tier 3 Change Workflow plan: client-only CPU/GLSL/Vulkan integration crosses command recording,
render-pass/load semantics, descriptor/layout synchronization, history ownership, and Terrain/Water sampling.
It has no simulation CRC, wire, save/replay, `.pack`, or server affinity exposure.

## Design

- Preserve record-once command buffers. Per-frame window variation flows through uniform data, indirect
  compute commands, and shader-generated bounded graphics geometry; do not add command-buffer rerecording or
  rely on dynamic scissor.
- Derive clamped integer texel rectangles from the current visible area for every lighting texture extent.
  Use floor/ceil coverage, guard zero and out-of-bounds dimensions, and preserve the full headroom allocation.
  Carry the current valid rectangle and the prior-history valid rectangle explicitly in the shared CPU/GLSL
  layout rather than inferring validity from area alone.
- Add `LightingClear.comp` to zero the three deposit storage images only over the active rectangle before the
  deposit render pass, then change the deposit render pass to `LOAD`. Area/point world quads and HexShield
  meshes already project localized geometry, so do not clamp or distort them: add an early half-open
  `gl_FragCoord` rectangle discard to `AreaLight.frag`, `PointLight.frag`, and `Objects/HexShieldLighting.frag`
  so no deposit write escapes the cleared rectangle. This bounds writes, not their existing localized raster cost.
- Compute spread rectangles by walking backward from the final consumer-visible rectangle through every enabled
  spread pass. Include each pass's interpolated spread distance, jitter reach, linear-filter footprint, and its
  mixed source/destination extent when expanding the successor requirement. Clamp each result to that pass's
  extent. `QuadsLightingWindowed.vert` is used only by spread: fixed push `.xy` holds extent and `.z` the pass
  index, which indexes the per-pass GlobalLayout bounds; it maps quad UV edges to the clip rectangle while
  retaining full-texture source UVs. All six spread MRT attachments change to `LOAD`, and every refresh draws
  the bounded pass rectangle even when deposit is empty. Remove the obsolete single-margin carry-forward branch.
  This 40-pass closure guarantees every in-window gather reads a freshly written predecessor while stale data
  outside the rectangle remains loaded but invalid.
- Convert combine and temporal to indirect window dispatches with an absolute-min-texel offset and in-shader
  bounds guards. They process their required valid rectangles rather than the full combine extent. Retain
  defensive bounds checks for indirect-command, ceil-to-tile, and recreate edge cases.
- Keep one persistent four-image history bank with static descriptors; do not rotate it. `LightingTemporal.comp`
  samples that bank and writes only combine. `LightingHistoryCopy.comp` then reads the four combine storage
  images and writes the four distinct history storage images only over the current valid rectangle. Record the
  corresponding layout transitions and barriers in the same ordering as the shadow history copy, so temporal
  never samples and writes the same image and the next history publication is synchronized.
- Establish the cadence refresh predicate in `RenderLightingGlobal` before `PopulateLightingParameters` and the
  area/valid-bounds latch. On a refresh, advance and publish `{area, validBounds}` together, then refresh
  exactly that history rectangle after temporal; on a cadence skip, deposit clear and localized raster continue
  every frame, but advance neither area, bounds, nor history; suppress spread through its indirect draw count;
  and write zero groups for combine, temporal, and history copy. The next refresh clears and rebuilds the
  active chain, intentionally leaving skip-frame deposits unconsumed/dropped. Recreate invalidates the latch
  and forces first refresh, blend `1`, and no history fetch. The later deposit-count writer consumes this
  established predicate rather than making the latch decision solely in `RenderLightingSpreadIndirect`.
- Update every lighting consumer to reject a sample unless its full linear-filter footprint lies inside the
  current valid rectangle. Terrain and Water must return the established no-light result for invalid direct,
  base-height, ambient, and reflected sample paths, preventing stale in-texture data from leaking after pan,
  zoom, resize, or recreate.

## Critical files

- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — record bounded deposit clear/raster,
  spread geometry, indirect combine/temporal/history copy, and exact synchronization without rerecording.
- `Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp` and `RenderTargetTextures.h` — deposit
  and six-spread-attachment `LOAD` semantics, one persistent distinct history bank, descriptor-compatible
  usages, and recreate invalidation.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` and `.h` — bounded-clear, deposit/spread,
  combine/temporal/history-copy descriptor and pipeline contracts.
- `Engine/Source/Graphics/Render/LightingUniforms.cpp` and `Render.h` — rectangle derivation, backward spread
  closure, refresh predicate, indirect commands, current/previous-valid bounds, and latch publication.
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — CPU/GLSL rectangle, per-pass bound, and valid-history layout fields.
- `Engine/Data/Shaders/Lighting/AreaLight.frag`, `PointLight.frag`, `LightingClear.comp`, `LightingSpread.frag`,
  `LightCombine.comp`, `LightingTemporal.comp`, and `LightingHistoryCopy.comp`; `Engine/Data/Shaders/Objects/HexShieldLighting.frag`; and `Engine/Data/Shaders/Quads/QuadsLightingWindowed.vert` — bounded deposit,
  clear/spread geometry, absolute indirect coordinates, dependency, history, and temporal-validity guards.
- `Engine/Data/Shaders/Terrain/Terrain.frag`, `Water/Water.frag`, and `ShaderFunctions.h` — linear-footprint
  valid-window guards for all lighting consumers.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `.filters` — new
  shader membership and client-only affinity verification.

## Out of scope

- Shadow dispatch, blur, and shadow temporal work — active `WindowedLightingShadowDispatch.md` owns the
  approved shadow-only split and is not edited here.
- Lighting refresh cadence, slider behavior, and its intentional skipped-frame visual tradeoff — owned by
  `LightingRefreshCadence.md`.
- Object shadows, spread-kernel quality/cost tuning, texel-ramp policy, and changing full lighting texture
  allocation.
- Simulation, determinism/CRC, networking, save/replay compatibility, asset packing, and server code.

## Acceptance criteria

- At settled camera height, GPU captures show bounded deposit clear, 40-pass spread, combine, temporal, and
  history-copy work scale with their bounded rectangles rather than the full allocation; existing lighting
  timers and active-pixel diagnostics report the covered footprint. Do not claim that the already-localized
  deposit quad/mesh raster cost itself scales with the rectangle.
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

- `Documents/Plans/Graphics/WindowedLightingShadowDispatch.md`: directional prerequisite. Do not start this
  plan while that claimed Plan exists; its completion preparation removes this direct metadata edge.
- `Documents/Plans/Graphics/LightingRefreshCadence.md`: never interleave. Both plans own
  `CommandBufferRecordMain` combine/temporal recording and `LightingUniforms` population. Co-schedule one
  session or land sequentially; the later plan refreshes shared-site citations and extends the first change
  instead of duplicating its indirect-dispatch conversion.
- `Documents/Plans/Graphics/Managers/Architecture_BindlessSlotLifecycle.md`: mandatory exclusion. That plan
  executes alone, so do not co-schedule or interleave pipeline/descriptor changes with this plan.

## Notes

- Existing host-visible indirect-compute plumbing is already present; this plan wires the lighting-specific
  rectangles and passes rather than recreating that helper.
- This records approved out-of-scope debt, not an acceptance failure of the shadow-only implementation.
