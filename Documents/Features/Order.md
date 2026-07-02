# Feature Execution Order

All brand-new additions — new render passes, new effects, new systems, new collections, new network/audio capabilities, new dev tooling that ships in the binary — sorted by score (lowest = highest priority).

Score = Effort − Impact + Risks (lower = higher priority)

`Tier` is an informal size/risk descriptor (**Quick Win** / **Small** / **Medium** / **Large** / **Architectural**); it does not affect score or ordering.

Counterpart: `Documents/Plans/Order.md` holds refactor/bugfix plans. See `Documents/CLAUDE.md` for the distinction. Scores are not comparable across the two files — they were originally derived from a single sort and then split.

## Plans

| Plan | Tier | Effort | Impact | Risks | Score | Notes |
|------|------|--------|--------|-------|-------|-------|
| [Graphics/SkyboxRenderPass.txt](Graphics/SkyboxRenderPass.txt) | Small | 1 | 4 | 0 | -3 | Fullscreen skybox draw (cubemap exists, only used for reflections). Star field at night, sun/moon disc using existing fSunAngle. |
| [Graphics/HdrResolveAndColorGrading.txt](Graphics/HdrResolveAndColorGrading.txt) | Medium | 3 | 5 | 1 | -1 | Render to F16 intermediate, ACES tone mapping resolve (dead code exists in Model.frag), 3D LUT color grading. Unlocks all HDR effects. |
| [Graphics/HeatDistortionAndShockwave.txt](Graphics/HeatDistortionAndShockwave.txt) | Small | 2 | 3 | 0 | -1 | Screen-space UV distortion near heat/explosions. Displacement-map shockwave for explosions. |
| [Engine/GradientParticlesAndCurlNoise.txt](Engine/GradientParticlesAndCurlNoise.txt) | Small | 2 | 3 | 0 | -1 | Lifetime color gradient for particles (white->orange->smoke). Curl noise in smoke spread shader. |
| [Graphics/TerrainSnowInGrooves.txt](Graphics/TerrainSnowInGrooves.txt) | Small | 1 | 2 | 0 | -1 | Curvature-aware snow in Terrain.frag using AO/normal derivatives instead of color-based detection. |
| [Misc/SpirvOptIntegration.txt](Misc/SpirvOptIntegration.txt) | Small | 1 | 2 | 0 | -1 | Add spirv-opt pass to DataPacker shader compilation. Reduces shader size, improves driver compile. |
| [Graphics/ocean-phase-3-foam.md](Graphics/ocean-phase-3-foam.md) | Small | 2 | 4 | 1 | -1 | Height+shore+slope foam factor modulated by noise pattern, blended pre-shadow in `Water.frag`; ~10 new floats + `TweaksScreenWaterFoam.cpp`. Depends on Phase 1. |
| [Graphics/ocean-phase-4-sun-glitter.md](Graphics/ocean-phase-4-sun-glitter.md) | Small | 1 | 3 | 1 | -1 | Noise-jittered micro-normal sparkle via NdotH smoothstep threshold added to Ward specular; ~5 new floats. Depends on Phase 1. |
| [Graphics/ocean-phase-6-sss.md](Graphics/ocean-phase-6-sss.md) | Small | 1 | 2 | 0 | -1 | Wave-height SSS with view-dependent sun alignment (`pow(1-VdotL)`) additive turquoise tint; ~8 new floats. Depends on Phase 1. |
| [Graphics/WaterFoamAndRefraction.txt](Graphics/WaterFoamAndRefraction.txt) | Medium | 3 | 4 | 1 | 0 | Foam/whitecaps from wave steepness, refraction via UV-distorted scene sampling. |
| [Audio/AdaptiveMusic.txt](Audio/AdaptiveMusic.txt) | Small | 2 | 2 | 0 | 0 | GetNextMusicTrack considers game state (combat, location). Crossfade infrastructure exists. |
| [Network/RemoteEntityInterpolation.txt](Network/RemoteEntityInterpolation.txt) | Medium | 3 | 4 | 1 | 0 | Snapshot-based interpolation for remote entities. |
| [Graphics/ocean-phase-5-caustics.md](Graphics/ocean-phase-5-caustics.md) | Small | 2 | 3 | 1 | 0 | Shallow-water two-layer noise `min()` cellular caustics modulated by depth smoothstep, added to sea-floor albedo; ~9 new floats. Depends on Phase 1. |
| [Graphics/ocean-phase-7-legacy-recovery.md](Graphics/ocean-phase-7-legacy-recovery.md) | Small | 2 | 3 | 1 | 0 | Re-integrate noise color-zone modulation, depth-LUT blend slider, and ambient-floor clamp on top of Phase 1 PBR; ~9 new floats. Depends on Phase 1. |
| [Graphics/DecalSystem.txt](Graphics/DecalSystem.txt) | Medium | 3 | 3 | 1 | 1 | Projected texture rendering for terrain marks (bullet holes, scorches, tracks). New pipeline pass. |
| [Engine/AddTracyProfiler.txt](Engine/AddTracyProfiler.txt) | Medium | 3 | 3 | 1 | 1 | Integrate Tracy profiler with existing profiling system. |
| [Graphics/ocean-phase-2-slope-mapping.md](Graphics/ocean-phase-2-slope-mapping.md) | Medium | 3 | 4 | 2 | 1 | 4-octave slope cascade (1077/154/22/3.1m) replacing 6 `SampleNormal()` calls; slope-variance → Ward roughness; noise-UV anti-tiling; ~10 new floats. Depends on Phase 1. |
| [Graphics/ocean-phase-1-pbr-foundation.md](Graphics/ocean-phase-1-pbr-foundation.md) | Architectural | 4 | 5 | 3 | 2 | Rewrite `Water.frag` main() to Beer-Lambert + Schlick + Ward BRDF + MeanFresnel; ~12 new `GlobalLayout` floats + new `TweaksScreenWaterPBR.cpp`; establishes shading model Phases 2-7 build on |
| [Graphics/FlipbookSpriteAnimations.txt](Graphics/FlipbookSpriteAnimations.txt) | Medium | 3 | 2 | 1 | 2 | Sprite-sheet animation for explosions (texture atlas with frame indexing). |
| [Graphics/DestructionBuffer.txt](Graphics/DestructionBuffer.txt) | Medium | 3 | 3 | 2 | 2 | Per-entity damage accumulation, fragment shader peels/blends sub-surface based on hit positions. |
| [Network/ForwardErrorCorrection.txt](Network/ForwardErrorCorrection.txt) | Medium | 4 | 3 | 1 | 2 | XOR-based FEC for unreliable coord updates. |
| [Frame/Future_TagBasedGenericIteration.txt](Frame/Future_TagBasedGenericIteration.txt) | Medium | 3 | 2 | 1 | 2 | C++ concepts for generic cross-collection operations. Revisit when 3+ more game collections added. |
| [Frame/Future_EventMessageBus.txt](Frame/Future_EventMessageBus.txt) | Medium | 3 | 3 | 2 | 2 | Frame-scoped event queue decoupling collection communication. Revisit when fan-out exceeds 3-4 consumers. |
| [Engine/GrassRendering.txt](Engine/GrassRendering.txt) | Large | 4 | 3 | 2 | 3 | Instanced grass patches, vertex shader height, noise textures for dryness/height. New collection + shaders. |
| [Engine/TreePlacementAndRendering.txt](Engine/TreePlacementAndRendering.txt) | Large | 4 | 3 | 2 | 3 | Tree/bush placement with LOD. New collection and instanced rendering. |
| [Frame/Future_SharedBehaviorTraits.txt](Frame/Future_SharedBehaviorTraits.txt) | Large | 4 | 3 | 2 | 3 | Reusable SOA trait structs composed into collections via tuple_cat with shared logic functions. Revisit at 20-25 collections. |
| [Frame/FrameRelativePositions.txt](Frame/FrameRelativePositions.txt) | Large | 5 | 5 | 3 | 3 | Convert world-absolute Frame positions to frame-relative + camera-frame-relative render merge (rebuild MergeFramesForRender/OffsetPositions). Fixes float precision degradation at distance (~1cm jitter at 140 frames from origin); enables larger maps. Requires save-format bump. |
| [Audio/ReplaceDirectXTKAudioWithMiniaudio.txt](Audio/ReplaceDirectXTKAudioWithMiniaudio.txt) | Large | 5 | 3 | 2 | 4 | Replace audio engine with miniaudio. Enables cross-platform (Linux/macOS/iOS/Android). |
| [Frame/Future_CollectionVariants.txt](Frame/Future_CollectionVariants.txt) | Large | 4 | 2 | 2 | 4 | Optional sparse SOA extensions for subset-only fields. No evidence of need currently. |

### Reference / Index Documents (not independently scheduled)

| Plan File | Purpose |
|-----------|---------|
| Graphics/ocean-fragment-shader-overview.md | Index/meta doc for ocean shader rewrite; links all 7 phase plans + Tweaks screen plan. Keep as reference. |
| Graphics/ocean-phase-tweaks-screen.md | UI-only consolidation spec (single `kWaterPBR` TweakSection with 7 tabs); subsumed by per-phase plans — each phase creates its own tab. Do not schedule standalone. |

## Dependencies

- `Graphics/ocean-phase-{2,3,4,5,6,7}` all depend on `Graphics/ocean-phase-1-pbr-foundation.md` (need Ward BRDF + Beer-Lambert + MeanFresnel to exist). Phases 2-7 are independent of each other; recommended visual-impact order: 2, 3, 4, 5, 6, 7.
- `Graphics/ocean-phase-tweaks-screen.md` is subsumed — each phase plan creates its tab inside the single `kWaterPBR` section; do not execute standalone.
- `Graphics/ocean-fragment-shader-overview.md` is an index/meta document, not an executable plan.

### Cross-directory dependencies (plans in `Documents/Plans/`)

- `Graphics/ocean-phase-{1,2,3,4,5,6,7}` touch `Water.frag`. No ordering constraint remains — the key guards (weighted-sum `normalize`, `pow`-skip, grazing-divide clamp) are already in `Water.frag`.

## File Groups

Plans that touch the same files and should be done in a single session:

- **`Engine/Data/Shaders/Water/Water.frag`**: all seven ocean phase plans (Phase 1 rewrites `main()`; Phases 2-7 add/modify terms).
- **`Engine/Data/Shaders/ShaderLayoutsBase.h` + `GlobalUniforms.cpp` + `WrapperBase.{h,cpp}` + `TweaksSliderMap.cpp`**: all seven ocean phase plans (each adds 5-12 floats; align ordering to minimize diff churn; single `kWaterPBR` TweakSection with 7 tabs).
