<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-03T02:45:42.000Z","dependsOn":[]} -->
# Water Half-Resolution Shading (offscreen shade + depth-aware composite)

## Context

`kGpuTimerWater` was the most expensive GPU pass at plan-creation time: **2329 µs current / 2248 avg / 2335 max** (Profile build, 120 fps, idle connected scene — ocean + islands, no combat), ~35% of the ~6.6 ms GPU frame (Terrain 998 µs, Objects 229 µs, Image pass total 3606 µs). The cost is dominated by per-fragment work at near-full-screen coverage: `Water.frag` does ~22 texture fetches plus heavy ALU per fragment (wave-normal octaves via `SAMPLE_NORMAL_PRECISE` `textureGrad` sums, EWNS lighting, analytic-AA specular lobes tuned by `fWaterSpecAAVariance`/`fWaterSpecAAThreshold`, reflected projection). Idle-scene caveat: water coverage and shading cost are effectively scene-independent in this ocean game, so the number generalizes.

Shading water at half resolution and upsampling is the established big lever for exactly this profile (quarter the fragment invocations). It is the largest single win available on this pass (historical estimate ~1.2–1.5 ms), but also the most architectural — hence a separate plan from the earlier mesh-density and cheap-fragment work. Those earlier optimizations have since changed the baseline, so **the first implementation step is mandatory: re-measure `kGpuTimerWater` on the current tree** (Profile build, 120 fps, idle connected scene) and scale this plan's budget model from the fresh number. If the fresh number is below ~1.0 ms, stop and report to the user before implementing — the payoff may no longer justify the added passes.

Key constraint inventory (all satisfiable): record-once command buffers (all new passes are static; per-frame variation stays in uniforms/indirect buffers, matching the existing water indirect-draw + indirect-dispatch pattern); snap-grid stability (untouched — visible area, LOD latch, and quad sizes are unchanged); water is render-only (no determinism exposure). Water currently draws in `CommandBufferRecordMain::RecordImageRenderPass` between the Terrain and HexShields draws, with pipeline flags `{kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias, kUpdateAfterBind, kIndirectHostVisible}` — later passes rely on the depth it writes, and its alpha encodes the shore terrain-fade.

## Scope contract

The listed scope is both target and ceiling: implement the smallest complete change satisfying the acceptance criteria; add no abstractions, configuration beyond the one listed setting, refactors, or fixes to adjacent code encountered along the way. Naming a file grants permission to touch only the named regions plus the mechanical necessities (includes, forward declarations, enum entries, vcxproj/filter membership) the named change requires.

**In scope**

- New shader files under `Engine/Data/Shaders/Water/`: a depth-only water fragment shader (modeled on `Engine/Data/Shaders/Ui/UiDepthPrepass.frag`), and a composite `.vert`/`.frag` fullscreen-triangle pair. `Water.frag` and `Water.vert` shading logic is unchanged; touch them only if the composite/depth split requires moving an existing declaration into a shared include.
- `Engine/Source/Graphics/Managers/PipelineManager.h`: new `kPipelineWaterDepth` and `kPipelineWaterComposite` entries in the pipeline enum next to `kPipelineWater` (the existing `kPipelineWater` becomes the offscreen shade pipeline; rename to `kPipelineWaterShade` only if the reviewer prefers — trivial naming).
- `Engine/Source/Graphics/Managers/PipelineManager.cpp`, `CreateLightingShadowDependentPipelines()` water block only (the `mpPipelines[kPipelineWater].Create(...)` call and the normal-map atlas setup feeding it): split into the three pipeline creations described in Design.
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`: `Record()` (insert the offscreen shade pass after the `RecordWaterDisplacement` call, before the Image render pass) and `RecordImageRenderPass()` (replace the single `pPipelines[kPipelineWater].RecordDrawIndirect` at the `kGpuTimerWater` slot with depth-only draw + composite; keep `kGpuTimerWater` GpuStart/GpuStop wrapping all water GPU work so the metric stays comparable). A new `RecordWater*` private member function mirroring `RecordWaterDisplacement` is the expected shape for the offscreen pass.
- `Engine/Source/Graphics/Managers/RenderTargetTextures.h`/`.cpp`: one new half-swapchain-extent color `Texture` member plus its creation (new `CreateWater*` function or an addition to `Create()`), recreated on resize/settings like other screen-sized targets.
- `Engine/Source/Graphics/Render/MainUniforms.cpp`, `RenderFrameMain()` water LOD block only (the `mWaterMeshLods[iLevelOfDetail]` lookup, `WriteIndirectBuffer` call, `iWaterActiveQuadX/Y` population, and the `kPipelineWaterDisplacement` `WriteIndirectComputeBuffer` call): add a second indirect-draw entry selecting the coarser shade LOD (see Design) and whatever quad-dimension uniforms the coarse mesh needs.
- `Engine/Source/Ui/GraphicsSettingsWrappersBase.h`/`.cpp`: one new discrete `Wrapper gWaterShadingResolution` (values {1/2, 1}, default 1 = off), declared and defined alongside `gWaterShapeDetail`.
- `Engine/Source/Graphics/Graphics.cpp`, `Graphics::Refresh()`: one `PollSetting` line for `gWaterShadingResolution` at the `DestroyType::kPipelines` tier, patterned on the existing `gWaterShapeDetail` line.
- `Engine/Source/Profile/ProfileManagerBase.h`: a new GPU timer enum entry for the offscreen shade pass only if decision (e) below resolves to yes.
- CPU/GLSL shared layout additions (uniforms for coarse quad dims, if decision on `iWaterShadeQuad*` requires them) in the existing shared shader-layout headers, plus DataPacker pickup and vcxproj/filter membership for the new shader files.

**Out of scope**

- Water mesh density defaults, `gWaterShapeDetail` values, or any 1/8-detail option (historical prerequisite plan `WaterMeshDensityOvershading.md` no longer exists in the tree; do not resurrect it).
- Further fragment fetch/ALU tuning inside `Water.frag` — independent work that multiplies with this plan.
- Any change to wave simulation, displacement prebake (`WaterDisplacement.comp`), snap grid, or `WaterFullDetail` anchoring.
- Temporal upsampling / checkerboard / ML upscaling of the water layer (bilinear + edge-aware only).
- Applying the same split to Terrain or any other pass.
- Any behavior change when `gWaterShadingResolution == 1` (default): the full-res path must remain the single draw it is today or an exactly equivalent recorded sequence.

## Design

Split the single full-res draw into three static pieces:

1. **Full-res depth-only water draw** (Image pass, at the current water slot): same water mesh + `Water.vert` displacement-`texelFetch` path, fragment stage empty except the over-land elevation `discard` guard already at the top of `Water.frag`'s `main()` (`fTerrainElevation > fWaterEarlyOut`) — or fully empty, accepting depth writes over land where terrain already won the depth test. Preserves byte-for-byte the depth surface later passes see today (`kDepthWrite`, `kDepthBias`, displaced crests occluding correctly against ships/hexshields/particles). Precedent: `kPipelineUiDepthPrepass` is already a depth-only pipeline with an empty fragment shader.
2. **Half-res offscreen shading pass**: new color RTT at half swapchain extent (RGBA16F to preserve the HDR-ish lighting sums; see decision (a)), rendering the full current `Water.frag` shading including the shore-fade alpha. Drive it with a **coarser LOD from the existing concat mesh** — a second indirect-draw entry selecting `mWaterMeshLods[min(iLevelOfDetail + N, BufferManager::kiVisibleAreaLodCount - 1)]` (N per decision (b)), mirroring how `RenderFrameMain` writes the active LOD today. Reusing the fine LOD mesh at half res would produce ~2.5 px triangles and catastrophic quad overshading, inverting the win. No depth attachment needed: the shader's existing elevation early-out `discard` bounds over-land waste to one fetch, and under-island pixels are resolved at composite time by the main depth buffer.
3. **Full-res composite** (Image pass, immediately after the depth-only draw): fullscreen triangle sampling the half-res color with an edge-aware upsample (elevation-guided at shorelines where the alpha gradient is steepest; plain bilinear elsewhere), alpha-blended with the same blend state water uses today, depth-tested against the just-written water depth (scheme per decision (c)) so it lands only on visible water pixels.

Budget model (rebaseline first; historical numbers): half-res shading ≈ baseline/4, + depth-only full-res raster ≈ ~150–250 µs, + composite ≈ ~80–150 µs. Against the historical 2329 µs baseline that totaled ≈ 0.8–1.0 ms, saving ~1.3–1.5 ms. Measure at each step against the fresh baseline.

Plumbing notes:

- New RTT + pipelines follow the existing destroy/recreate tiers (`RenderTargetTextures`, `PipelineManager`, `CommandBufferRecordMain`); half-res extent derives from the swapchain extent, recreated on resize like every screen-sized target.
- `Water.frag`'s screen-derivative machinery (`textureGrad` mips, spec-AA kernels) automatically sees the 2x footprint at half res — mips coarsen one level and the analytic specular filters widen correspondingly, which is the correct antialiased behavior; `fWaterSpecAAVariance` remains the tuning knob if the lobes need rebalancing.
- The vertex-varying set feeding the shading pass is unchanged; only `gl_Position` viewport scale differs (same visible area).
- The displacement texture is 1:1 with the *fine* grid (`iWaterActiveQuadX/Y`); the coarse shade mesh must sample it at its own stride (decision (f): dedicated coarse `iWaterShadeQuad*` uniforms vs reusing the texel-alignment math).
- MSAA interaction: if the Image pass runs multisampled (`gSampleCount`), the depth-only water draw inherits it; the composite is a fullscreen blend and needs no per-sample work (water was never sample-shaded — the existing `PipelineManager.cpp` comment confirms `kSampleShading` stays off for water). Confirm at decision (d).
- Runtime setting: new discrete wrapper `gWaterShadingResolution` {1/2, 1} (extendable to {1/4} later — do not add now), **default 1 (off) until A/B**, routed through `Graphics::Refresh` `PollSetting` at the pipelines destroy tier exactly like `gWaterShapeDetail`, so users trade water sharpness for the measured saving.

### Visual impact (mandatory statement)

**(c) at close zoom, (b) at gameplay heights.** Normal-map glint detail, color noise, and shore-fade edges are shaded with a 2x pixel footprint — visibly softer when zoomed fully in; at typical RTS camera heights the wave-normal octaves are already near the mip floor and the difference is hard to see. Geometric crest silhouettes stay **full-res** (depth-only draw uses the fine LOD), which is what reads most at a top-down camera. Mitigations: edge-aware upsample at shorelines; spec-AA variance retune; setting defaults off.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` / `Water.vert` — shading unchanged; new thin depth-only fragment and a new composite `.vert`/`.frag` pair under `Engine/Data/Shaders/Water/` (DataPacker picks the directory up automatically)
- `Engine/Source/Graphics/Managers/PipelineManager.h` / `.cpp` — pipeline enum entries; `CreateLightingShadowDependentPipelines()` water block split into depth/shade/composite creations; the shade pass keeps the current water descriptor set; composite binds the half-res RTT + elevation texture
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — `Record()` for the offscreen shade pass (after `RecordWaterDisplacement`); `RecordImageRenderPass()` for depth-only + composite at the current `kGpuTimerWater` slot, timer wrapping all three
- `Engine/Source/Graphics/Managers/RenderTargetTextures.h` / `.cpp` — new half-res water color RTT, recreated on resize/settings
- `Engine/Source/Graphics/Render/MainUniforms.cpp` — `RenderFrameMain()` water LOD/indirect block: second indirect-draw entry for the coarse shade LOD; coarse quad-dimension uniforms per decision (f)
- `Engine/Source/Ui/GraphicsSettingsWrappersBase.h` / `.cpp` + `Engine/Source/Graphics/Graphics.cpp` `Graphics::Refresh()` — `gWaterShadingResolution` wrapper + `PollSetting`
- `Engine/Source/Profile/ProfileManagerBase.h` — shade-pass GPU timer only if decision (e) says yes

## Risk tier and invariants

Tier 2 — scoped behavior in the graphics subsystem, client/render-only. No determinism/CRC, wire, save/replay, or threading exposure; reviewer may escalate if the changed bytes expose more.

Invariants to preserve:

- **Shader repack required** (new shaders + any shared-layout additions); new pipeline enum entries and vcxproj filter additions for new shader files.
- All new passes are record-once — no per-frame command-buffer recording; per-frame variation only via uniforms/indirect buffers.
- RTT/pipeline creation at destroy tiers only — no steady-state allocation (allocation tracking will trap violations).
- Depth surface seen by passes after water is unchanged when the setting is on; full-res path byte-identical when off (default).
- `kGpuTimerWater` continues to cover all water Image-pass GPU work so profiles stay comparable.

## Acceptance criteria

1. Fresh `kGpuTimerWater` baseline recorded before implementation; final measurement shows the half-res path total (shade + depth-only + composite) materially below that baseline, with per-pass numbers reported.
2. With `gWaterShadingResolution == 1` (default), rendering is unchanged (screenshot compare vs pre-change build at the same scene/camera).
3. With `gWaterShadingResolution == 1/2`, water renders without shoreline halos, under-island bleed, or crest-silhouette softening at a top-down gameplay camera (harness screenshots at close zoom and gameplay height).
4. Toggling the setting at runtime recreates pipelines/RTTs without validation errors; window resize recreates the half-res target correctly.
5. Client builds clean; server build unaffected (all changes client/graphics-only).

## Unresolved decisions (pre-staged for review; original wording preserved)

Grill/review decisions the implementer must have adjudicated before or during plan review — do not pick silently:

- (a) offscreen color format (RGBA16F vs R11G11B10 + separate alpha);
- (b) coarse-LOD offset for the shade mesh (+1 vs +2);
- (c) composite depth-test scheme (`EQUAL` on the depth-only surface vs re-evaluating the over-land discard);
- (d) MSAA interaction (verify the composite-needs-no-per-sample-work claim);
- (e) whether the shade pass gets its own GPU timer;
- (f) dedicated coarse `iWaterShadeQuad*` uniforms vs reusing the texel-alignment math.

## Notes

- Historical sequencing: this plan originally depended on `WaterMeshDensityOvershading.md` (mesh-density measurement + 1/8 option) landing first; that plan file no longer exists in the tree. The surviving obligation is the mandatory rebaseline in Context.
- Research grounding: half/quarter-res shading + upsample as the standard pattern for expensive screen-coverage effects — [Unreal's Rendering Passes / translucency and SSAO half-res practice](https://unrealartoptimization.github.io/book/profiling/passes/); quad overshading motivating the coarse shade mesh — [Fatahalian et al.](https://graphics.stanford.edu/papers/fragmerging/shade_sig10.pdf); geometry-vs-normal-map LOD tradeoff for Gerstner oceans — [GPU Gems ch. 1, Effective Water Simulation](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models).
