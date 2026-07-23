<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-03T02:45:42.000Z","dependsOn":[]} -->
# Water Half-Resolution Shading (offscreen shade + depth-aware composite)

## Context

`kGpuTimerWater` is the most expensive GPU pass: **2329 µs current / 2248 avg / 2335 max** (Profile build, 120 fps, idle connected scene — ocean + islands, no combat), ~35% of the ~6.6 ms GPU frame (Terrain 998 µs, Objects 229 µs, Image pass total 3606 µs). The cost is dominated by per-fragment work at near-full-screen coverage: `Water.frag` does **22 texture fetches** plus heavy ALU per fragment (9 wave-normal octaves, 3 EWNS lighting, analytic-AA specular lobes, reflected projection). Idle-scene caveat: water coverage and shading cost are effectively scene-independent in this ocean game, so the number generalizes.

Shading water at half resolution and upsampling is the established big lever for exactly this profile (quarter the fragment invocations; the SSAO/volumetrics precedent). It is the largest single win available on this pass (**est. ~1.2–1.5 ms**), but also the most architectural — hence a separate plan from the mesh-density work and the now-landed cheap fragment optimizations. The mesh-density work should land and be measured first; this plan must also rebaseline the water timer because the landed fragment savings reduce its payoff and may resize its Impact.

Key constraint inventory (all satisfiable): record-once command buffers (all new passes are static; per-frame variation stays in uniforms/indirect buffers, matching the existing water indirect-draw + indirect-dispatch pattern); snap-grid stability (untouched — the visible area, LOD latch, and quad sizes are unchanged); water is render-only (no determinism exposure). Water currently draws with `{kAlphaBlend, kCullBack, kDepthTest, kDepthWrite, kDepthBias}` between Terrain and HexShields in the swapchain Image pass — later passes rely on the depth it writes, and its alpha encodes the shore terrain-fade.

## Design

Split the single full-res draw into three static pieces:

1. **Full-res depth-only water draw** (Image pass, at the current water slot): same water mesh + `Water.vert` displacement-`texelFetch` path, fragment stage empty except the over-land `discard` guard (or fully empty, accepting depth writes over land where terrain already won the depth test). Preserves byte-for-byte the depth surface later passes see today (`kDepthWrite`, `kDepthBias`, displaced crests occluding correctly against ships/hexshields/particles). Precedent: `UiDepthPrepass` is already a depth-only pass with an empty fragment shader.
2. **Half-res offscreen shading pass**: new color RTT at half swapchain extent (RGBA16F to preserve the HDR-ish lighting sums; grill: RGBA16F vs R11G11B10 + separate alpha), rendering the full current `Water.frag` shading including the shore-fade alpha. Drive it with a **coarser LOD range from the existing concat mesh** (`mWaterMeshLods[iLod + 2]` via its own indirect-draw entry, mirroring how `MainUniforms` writes the active LOD today) — reusing the LOD0 mesh at half res would produce ~2.5 px triangles and catastrophic quad overshading, inverting the win. No depth attachment needed: the shader's existing elevation early-out `discard` bounds over-land waste to one fetch, and under-island pixels are resolved at composite time by the main depth buffer.
3. **Full-res composite** (Image pass, immediately after the depth-only draw): fullscreen triangle sampling the half-res color with an edge-aware upsample (elevation-guided at shorelines where the alpha gradient is steepest; plain bilinear elsewhere), alpha-blended with the same blend state water uses today, depth-tested `EQUAL`/`LEQUAL` against the just-written water depth so it lands only on visible water pixels.

Budget model (measure at each step): half-res shading ≈ 2329/4 ≈ **~580 µs** + depth-only full-res raster ≈ **~150–250 µs** (less if the density plan landed) + composite ≈ **~80–150 µs** → total ≈ **0.8–1.0 ms vs 2329 µs → saves ~1.3–1.5 ms**, by far the largest available water win.

Plumbing notes:

- New RTT + pipelines follow the existing destroy/recreate tiers (`RenderTargetTextures`, `PipelineManager`, `CommandBufferRecordMain`); half-res extent derives from the swapchain extent, recreated on resize like every screen-sized target.
- `Water.frag`'s screen-derivative machinery (`textureGrad` mips, spec-AA kernels) automatically sees the 2x footprint at half res — mips coarsen one level and the analytic specular filters widen correspondingly, which is the correct antialiased behavior; `fWaterSpecAAVariance` remains the tuning knob if the lobes need rebalancing.
- The vertex-varying set feeding the shading pass is unchanged; only `gl_Position` viewport scale differs (same visible area).
- MSAA interaction: if the Image pass runs multisampled (`gSampleCount`), the depth-only water draw inherits it; the composite is a fullscreen blend and needs no per-sample work (water was never sample-shaded — `kSampleShading` stays off). Verify at grill.

### Visual impact (mandatory statement)

**(c) at close zoom, (b) at gameplay heights.** Normal-map glint detail, color noise, and shore-fade edges are shaded with a 2x pixel footprint — visibly softer when zoomed fully in; at typical RTS camera heights the wave-normal octaves are already near the mip floor and the difference is hard to see. Geometric crest silhouettes stay **full-res** (depth-only draw uses the fine LOD), which is what reads most at a top-down camera. Mitigations: edge-aware upsample at shorelines; spec-AA variance retune. Runtime slider: **yes** — new discrete wrapper `gWaterShadingResolution` {1/2, 1} (extendable to {1/4}), **default 1 (off) until A/B**, routed through `Graphics::Refresh` `PollSetting` at the pipelines destroy tier exactly like `gWaterShapeDetail`, so users trade water sharpness for ~1.4 ms.

## Critical files

- `Engine/Data/Shaders/Water/Water.frag` / `Water.vert` — unchanged shading; new thin depth-only fragment (or reuse pattern from `Ui/UiDepthPrepass.frag`) and a new composite `.vert/.frag` pair under `Engine/Data/Shaders/Water/`
- `PipelineManager::CreatePipelines` water block (`Engine/Source/Graphics/Managers/PipelineManager.cpp`) — split `kPipelineWater` into `kPipelineWaterDepth` / `kPipelineWaterShade` (offscreen) / `kPipelineWaterComposite`; descriptor sets for the shade pass are the current water set; composite binds the half-res RTT + elevation
- `CommandBufferRecordMain::Record` (`Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`) — offscreen shade pass recorded before the Image pass (after `kGpuTimerWaterDisplacement`); depth-only + composite at the current `kGpuTimerWater` slot; keep the GPU timer wrapping all three so the metric stays comparable
- `RenderTargetTextures` (`Engine/Source/Graphics/Managers/RenderTargetTextures.cpp`) — new half-res water color RTT (+ render pass), recreate on resize/settings
- `MainUniforms` water LOD/indirect setup (`Engine/Source/Graphics/Render/MainUniforms.cpp`) — second `WriteIndirectBuffer` entry selecting `mWaterMeshLods[iLod + N]` for the shade pass; `iWaterActiveQuad*` handling for the coarser grid (the displacement texture is 1:1 with the *fine* grid — the coarse shade mesh must sample it at its own stride; grill: dedicated coarse `iWaterShadeQuad*` uniforms vs reusing the texel-alignment math)
- `engine::gWaterShadingResolution` (new) — `Engine/Source/Ui/GraphicsSettingsWrappersBase.cpp` / `.h` + `Graphics::Refresh` `PollSetting`
- `Engine/Source/Profile/` GPU timer enums only if a separate shade-pass timer is wanted (optional)

## Out of scope

- Mesh density defaults and the 1/8 detail option (`WaterMeshDensityOvershading.md` — prerequisite measurement, land first)
- Further fragment fetch/ALU tuning — the current cheap fragment optimizations have landed; additional tuning remains independent and multiplies with this plan
- Any change to wave simulation, displacement prebake, snap grid, or `WaterFullDetail` anchoring
- Temporal upsampling / checkerboard / ML upscaling of the water layer (bilinear + edge-aware only)
- Applying the same split to Terrain or other passes

## Notes

- Invariant exposure: client/graphics-only; **shader repack required** (new shaders + any layout additions); new `kPipeline*` enum entries and vcxproj filter additions for new shader files (DataPacker picks up `Engine/Data/Shaders/Water/` automatically); no determinism/CRC/`kiVersion`/wire exposure; all new passes are record-once (no per-frame CB recording); RTT creation at destroy tiers only (no steady-state allocation).
- Sequencing: execute **after** `WaterMeshDensityOvershading.md` (its measurement changes this plan's cost model and its 1/8 option reduces the depth-only draw cost), then rebaseline the now-optimized fragment pass before selecting the half-resolution design. Same-files group with the mesh-density work (`MainUniforms.cpp`, `GraphicsSettingsWrappersBase.cpp`).
- Pre-staged grill decisions: (a) offscreen color format (RGBA16F vs packed); (b) coarse-LOD offset for the shade mesh (+1 vs +2); (c) composite depth-test scheme (`EQUAL` on the depth-only surface vs re-evaluating the over-land discard); (d) MSAA interaction; (e) whether the shade pass gets its own GPU timer.
- Research grounding: half/quarter-res shading + upsample as the standard pattern for expensive screen-coverage effects — [Unreal's Rendering Passes / translucency and SSAO half-res practice](https://unrealartoptimization.github.io/book/profiling/passes/); quad overshading motivating the coarse shade mesh — [Fatahalian et al.](https://graphics.stanford.edu/papers/fragmerging/shade_sig10.pdf); geometry-vs-normal-map LOD tradeoff for Gerstner oceans — [GPU Gems ch. 1, Effective Water Simulation](https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models).
