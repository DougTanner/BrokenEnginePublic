# Water Mesh Density / Quad-Overshading Reduction

## Context

`kGpuTimerWater` (the single `kPipelineWater` indirect draw in the swapchain Image pass, `CommandBufferRecordMain.cpp`) is the most expensive GPU pass in the frame: **2329 µs current / 2248 avg / 2335 max** (Profile build, 120 fps, idle connected scene — ocean + islands, no combat), ~35% of the ~6.6 ms GPU frame. For scale: Terrain 998 µs, Objects 229 µs, whole Image pass 3606 µs; the `kGpuTimerWaterDisplacement` compute prepass is 106 µs. Caveat: measured on an idle scene, but water covers the screen in every scene, so the cost is effectively scene-independent.

Geometry anatomy: `WaterFullDetail()` (`Graphics.cpp`) anchors the water grid to a fixed 3840x2160 reference (Gerstner frequencies are resolution-independent), block-snapped to ≈3456 x ~2000. `gWaterShapeDetail` (default **1/4**, options {1/4, 1/2}) scales that via `TextureManager::WaterDetailTextureSize`, so the LOD0 mesh built by `BufferManager::CreateWaterMesh`/`BuildLodConcatMesh` is ≈864 x ~500 vertices → **≈0.43M vertex invocations and ≈0.86M triangles per frame when LOD0 is active** (gameplay eye heights; `CameraBase` LOD bucket).

At the 4K reference that is an average triangle of **~10 pixels**. GPUs shade in 2x2 pixel quads, so ~10 px triangles waste a large fraction of fragment-shader invocations on helper lanes (quad overshading — Fatahalian et al. measure up to 8x amplification at pixel-scale triangles; ~10 px triangles plausibly cost 1.3–2x). Water.frag is one of the heaviest fragment shaders in the engine (**22 texture fetches per fragment**: 9 wave-normal octaves, 3 EWNS lighting, elevation, skybox, shadow, object shadows, 2 smoke, 2 color noise, depth LUT, ambient — plus three analytic-AA specular lobes and the EWNS pow loop), so helper-lane waste is expensive. The vertex shader itself is already cheap (the displacement prebake landed: 2 `texelFetch` + 1 elevation `textureLod`), but 0.43M invocations plus raster setup for 0.86M triangles is still real work, and the displacement prepass texel count scales with the same grid.

## Design

Cheapest possible experiment against the top pass — a one-line option extension plus a measure-then-decide default:

1. Add `1/8` to the `gWaterShapeDetail` discrete option list (`GraphicsSettingsWrappersBase.cpp`): `{1.0f / 8.0f, 1.0f / 4.0f, 1.0f / 2.0f}`, default unchanged at `1/4`. The entire downstream path already exists and is runtime-switchable: `Graphics::Refresh`'s `PollSetting` routes the change to `DestroyType::kPipelines` + `DestroyFlags::kWaterMesh`, which recreates the mesh (`BufferManager::CreateWaterMesh`) and the paired displacement textures (`RenderTargetTextures::CreateWaterDisplacementTextures`) together — both call `WaterDetailTextureSize(gWaterShapeDetail.Get())`, so the 1:1 texel↔vertex alignment invariant is preserved automatically. Verify the `WaterDetailTextureSize` floor clamps (128 x 64) stay comfortably below the 1/8 result (~432 x ~250 — they do).
2. Measure `kGpuTimerWater` and `kGpuTimerWaterDisplacement` at 1/4 vs 1/8 across zoom levels (LOD0 through the deepest LOD in gameplay range). Expected at LOD0: vertex invocations and displacement texels /4 (prepass 106 → ~27 µs), triangles /4 (~10 → ~40 px/triangle, restoring healthy 2x2-quad utilization for the 22-fetch fragment shader). The fragment-side helper-lane recovery is the uncertain term; combined estimate **200–600 µs** of the 2329 — measure, don't assume.
3. Decision point (user judges): flip the default to 1/8 if visuals hold at the closest zoom, else keep 1/4 as default and ship 1/8 as a performance option.

Snap-grid invariant: quad counts per LOD still come from `BufferManager::mWaterMeshLods`; the `CameraBase` zoom-bucket/LOD latches keep `f2VisibleAreaQuadSize` bit-identical across frames exactly as today — density changes only at the settings-change destroy boundary (same event as the existing 1/4↔1/2 flip). The visible-area snap coarsens 2x per dimension (up to one coarser quad of extra overscan); elevation/shadow/lighting texture sizing is independent (`DetailTextureSize`, not `WaterDetailTextureSize`) and unaffected.

### Visual impact (mandatory statement)

**(b) minor/imperceptible at gameplay camera heights, with (c) risk only at the closest zoom.** Wave *shading* (normal maps, specular, color) is per-fragment and unchanged; only geometric crest displacement is sampled 2x coarser. The default low band (λ = 4 m, `gWaterLowWavelength`) stays well-sampled per wavelength at gameplay LODs; the medium band (λ ≈ 2 m) is default-off (`gWaterMediumAmplitude` = 0). At the closest zoom, crest silhouettes and the Z-driven trough darken/color height terms may read slightly coarser — that is the A/B gate in step 3. Runtime slider: **already exists** (`gWaterShapeDetail`, Tweaks graphics settings); suggested option set {1/8, 1/4, 1/2}, default 1/4 until the A/B verdict.

## Critical files

- `engine::gWaterShapeDetail` — `Engine/Source/Ui/GraphicsSettingsWrappersBase.cpp` / `.h` (the only required edit: extend the discrete option list)
- `Graphics::Refresh` `PollSetting(gWaterShapeDetail, ...)` — `Engine/Source/Graphics/Graphics.cpp` (existing destroy-tier routing; no change expected)
- `BufferManager::CreateWaterMesh` / `BuildLodConcatMesh` / `mWaterMeshLods` — `Engine/Source/Graphics/Managers/BufferManager.cpp` (consumes the new value; no change expected)
- `TextureManager::WaterDetailTextureSize` — `Engine/Source/Graphics/Managers/TextureManager.cpp` (verify floor clamps; no change expected)
- `RenderTargetTextures::CreateWaterDisplacementTextures` — `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` (paired recreate; no change expected)
- `CameraBase` LOD/zoom-bucket latch — `Engine/Source/Graphics/CameraBase.cpp` (read-only sanity check of the snap invariant)

## Out of scope

- Water.frag shading cost (see `WaterFragmentCostReduction.md`)
- Half-resolution water shading (see `WaterHalfResolutionShading.md` — that plan wants this one's density option available first)
- LOD table shape changes (number of LODs, per-LOD halving ratio, `kiVisibleAreaLodCount`)
- Elevation / shadow / lighting texture sizing (independent of the water grid)
- Wave parameter tuning (counts, wavelengths, amplitudes)

## Notes

- Invariant exposure: client/graphics-only; **no shader repack** (C++-only change); no determinism/CRC/`kiVersion`/`.pack`/wire exposure; mesh rebuild runs at the settings destroy tier, not in the allocation-tracked steady-state loop.
- Grill decision (single open item): after measurement, flip the default to 1/8 vs ship it as an option only.
- Research grounding: quad-fragment overshading at small triangles — [Fatahalian et al., Reducing Shading on GPUs using Quad-Fragment Merging](https://graphics.stanford.edu/papers/fragmerging/shade_sig10.pdf); [ryg, A trip through the Graphics Pipeline part 8](https://fgiesen.wordpress.com/2011/07/10/a-trip-through-the-graphics-pipeline-2011-part-8/); [GPU Performance for Game Artists](https://www.gamedeveloper.com/programming/gpu-performance-for-game-artists).
