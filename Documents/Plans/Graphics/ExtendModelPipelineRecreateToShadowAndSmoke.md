# Extend Pipeline Recreate To Shadow And Smoke Textures

## Context

The selective-recreation path in `PipelineManager::RecreatePipelineGroups` (`Engine/Source/Graphics/Managers/PipelineManager.cpp`) has produced two distinct flat-terrain / device-loss bugs in the lighting-textures flow that have **already been fixed** this session. Both fixes need to be mirrored to the analogous shadow and smoke flows or the same symptoms will re-surface when those sliders are dragged.

### Bug class 1 — `kModel` auto-append staleness

The `kModel` descriptor flag in `Engine/Source/Graphics/Objects/Pipeline.cpp:64-98` auto-appends three follow-on sampler bindings on every model pipeline:

```
[i+1]  combined sampler array  ← mppLightingFinalTextures   (= mpCombineTextures)
[i+2]  combined sampler        ← mShadowBlurTexture
[i+3]  combined sampler        ← mSmokeTextureOne
```

When the underlying texture's `Texture::Create` runs again, the `VkImageView` is replaced (Texture::Create internally calls `Destroy()` first per `Engine/Source/Graphics/Objects/Texture.cpp:169`), but the descriptor sets on every `kModel`-flagged pipeline cache the old `VkImageView` at write time. The driver eventually returns `VK_ERROR_SURFACE_LOST_KHR` from sampling undefined memory.

The lighting half is fixed: the `kLightingTextures` branch in `RecreatePipelineGroups` now runs a `mDynamicPipelines.mModelPipelineMaps[…]->Recreate()` loop after `CreateLightingPipelines()`. The same dependency exists for `mShadowBlurTexture` (recreated by `CreateShadowTextures` on the `kShadowTextures` flag) and `mSmokeTextureOne` (recreated by `CreateSmokeTextures` on the `kSmokeTextures` flag). Both paths use `bSelectiveRecreation` (only `kObjectShadows` opts out per `Graphics.cpp:591`), so they currently leave `kModel` pipelines with stale descriptors.

### Bug class 2 — terrain-data pipeline silent descriptor staleness

Separately, this session uncovered that the terrain-data writer pipelines (`kPipelineTerrainElevation` / `kPipelineTerrainColor` / `kPipelineTerrainNormal` / `kPipelineTerrainAmbientOcclusion`) silently lose their bindless `mElevationTextures` / `mColorTextures` / `mNormalsTextures` / `mAmbientOcclusionTextures` binding when sibling pipelines are recreated under `kLightingTextures`. No Vulkan validation error fires — the `VkImageView` handles stay live — but sampling yields zero. `Terrain.vert` reads zero from `mTerrainElevationTexture`, displaces every vertex to z=0, and the terrain renders as a flat plane.

The lighting half is fixed: the `bStageTerrainData` predicate in `RecreatePipelineGroups` now also includes `kLightingTextures`, so `CreateTerrainDataPipelines()` runs alongside `CreateLightingPipelines()` and the terrain-data descriptor sets are reallocated/rewritten. The exact mechanism (descriptor-pool ordering, sampler cache, etc.) is not fully characterized; the empirical fix is to reallocate the descriptor sets, which is what `Pipeline::Create` does. The same churn happens during the shadow and smoke recreate paths (they invoke `CreatePipelineShadows()` / dynamic smoke pipeline recreates that mutate the same pool), so the bug class is reachable from those sliders too even though the user has not yet reported it visually.

## Design

Mirror both fix patterns in the shadow and smoke stages.

### Shared helper for the model-pipeline recreate

To avoid three near-identical copy-pasted blocks (one for each of lighting/shadow/smoke), factor the model-pipeline recreate loop into a `private` helper on `PipelineManager`:

```cpp
// Recreate kModel-flagged pipelines because their cached descriptor sets reference
// VkImageView handles for mppLightingFinalTextures / mShadowBlurTexture / mSmokeTextureOne
// (auto-appended at Pipeline.cpp:64-98). Any flag that destroys+recreates one of those
// textures must call this, or kModel pipelines sample destroyed memory → SURFACE_LOST.
void PipelineManager::RecreateAllModelPipelines()
{
    for (DynamicModelPipelineType eType : {kDynamicModelPipelineModel, kDynamicModelPipelineModelShadow})
    {
        for (auto& [rCrc, rpModelPipeline] : mDynamicPipelines.mModelPipelineMaps[eType])
        {
            rpModelPipeline->Recreate();
        }
    }
}
```

Replace the existing inline kLightingTextures loop with a call to `RecreateAllModelPipelines()`, and add two more call sites:

1. After `CreatePipelineShadows()` (line ~793) guarded on `(flags & kShadowTextures)`.
2. After the dynamic smoke/wind deposit pipeline section (line ~830) guarded on `(flags & kSmokeTextures)`.

Ordering: each call must come **after** the corresponding `Create*Textures()` finishes so the helper reads the freshly-rebuilt `mppLightingFinalTextures` / `mShadowBlurTexture` / `mSmokeTextureOne` slots, not the dangling pre-recreate handles.

### Extend terrain-data recreate to shadow and smoke

Extend the `bStageTerrainData` predicate further:

```cpp
if ((flags & kTerrainElevation) || (flags & kTerrainColor) || (flags & kTerrainNormal) || (flags & kTerrainAO)
    || (flags & kLightingTextures) || (flags & kShadowTextures) || (flags & kSmokeTextures))
{
    CreateTerrainDataPipelines();
}
```

The cost is a per-slider-drag rebuild of four small pipelines, which is acceptable. The benefit is closing the bug class for all three sibling recreate flows symmetrically.

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.h` — declare `RecreateAllModelPipelines()` as a `private` member.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` `PipelineManager::RecreatePipelineGroups` — replace the inline kLightingTextures loop with the helper call, add two more call sites for kShadowTextures (after `CreatePipelineShadows`) and kSmokeTextures (after the smoke-deposit dynamic-pipeline loop); extend the `bStageTerrainData` predicate to include `kShadowTextures` and `kSmokeTextures`.
- `Engine/Source/Graphics/Objects/Pipeline.cpp:64-98` — comment site only; add a one-line cross-reference to `RecreateAllModelPipelines` so future kModel-flag readers know about the recreate contract.

## Out of scope

- The `kModel` auto-append model itself. Replacing it with explicit per-pipeline descriptor declarations would eliminate the need for any recreate-side bookkeeping but is a larger refactor and not justified by this bug alone.
- `TextureDescriptors::UpdateArrayBindingsForKey`-style in-place descriptor patching. The lighting half chose full pipeline recreate to match the existing stage idiom; this plan continues that choice for consistency.
- `kObjectShadows` — already excluded from `bSelectiveRecreation`, so it triggers a full `PipelineManager` rebuild and naturally rebuilds every pipeline.
- Characterising the exact descriptor-pool churn mechanism behind bug class 2. The empirical fix (reallocate the terrain-data pipelines' descriptor sets) is sufficient and matches the existing kModel idiom; deeper root-causing is a separate investigation.
- The `kLightingTextures` cases for both bug classes — both already fixed in this session.

## Acceptance criteria

- Dragging any cvar that fires `kShadowTextures` (shadow texture multiplier sliders in *Tweaks → Shadow*) or `kSmokeTextures` (`gSmokeSimulationPixels`, `gSmokeSimulationArea`, `gSmokeTrailPower`, `gSmokeTrailAlpha` in *Tweaks → Smoke*) produces **no `VK_ERROR_SURFACE_LOST_KHR`**, **no flat-terrain symptom**, and **no visual corruption on models**.
- A grep for `mModelPipelineMaps` in `PipelineManager.cpp` returns exactly one iteration site (the helper), not three copies.
- `bStageTerrainData`-equivalent predicate covers all of `kLightingTextures` / `kShadowTextures` / `kSmokeTextures` in addition to the explicit terrain-data flags.

## Notes

- Test path: in *Tweaks → Shadow*, change a shadow-texture-multiplier slider; in *Tweaks → Smoke*, change *Smoke Simulation Pixels*. Then sanity-check *Tweaks → Lighting → Write → Texture Multiplier Start* (the one that originally surfaced both bug classes) still works since this plan refactors that code path.
- The terrain-data half is what the user actually observed as the reported "flat terrain" bug; the kModel half is a latent bug observable as `SURFACE_LOST` plus subtler model-shading corruption. Test both surfaces.
