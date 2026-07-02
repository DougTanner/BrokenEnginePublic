# Terrain Snow Curvature Modulation

## Context

Snow placement is authored per-island in Gaea and shipped as the B channel of the BC7 material mask (`fSnowPercent = f4Masks.b`, `Terrain.frag`). The mask gives broad, art-directed placement, but its transition zones are smooth gradients — real snow clumps in grooves/crevices and scours off exposed ridges, which reads clearly even at RTS altitude.

The per-island AO texture (`ambientOcclusionTextureSamplers`, already fetched in `Terrain.frag` for the `SunLighting` occlusion factor) is a baked concavity proxy: dark = crevice/valley, bright = open face. Modulating the snow mask by it adds groove-scale snow detail with **zero additional texture fetches**.

Replaces the retired `TerrainSnowInGrooves.txt`, which targeted the old color-distance snow detection (removed when snow moved to authored masks).

## Design

All in `Terrain.frag`, plus one uniform's plumbing.

1. **Hoist the AO fetch** (`fAmbientOcclusionRaw`, currently fetched just before the shadow block) above the material-mask block — pure reorder, the fetch happens unconditionally today.
2. **Modulate the snow mask** right after `fSnowPercent = f4Masks.b`, before `fRockPercent`/`fBeachPercent` derive from it (so boosted snow suppresses rock/sand blends consistently, matching the existing snow-priority rule):

```glsl
// Concavity proxy: 1 in crevices, 0 on open faces
float fConcavity = 1.0f - fAmbientOcclusionRaw;
// Band term peaks at mask=0.5 and vanishes at 0 and 1, so fully-bare and fully-snow-painted
// pixels stay exactly as authored; only Gaea's transition gradients gain groove detail.
float fBand = 4.0f * fSnowPercent * (1.0f - fSnowPercent);
fSnowPercent = clamp(fSnowPercent + globalLayout.fTerrainSnowCurvature * fBand * (2.0f * fConcavity - 1.0f), 0.0f, 1.0f);
```

Signed modulation: crevices (`fConcavity > 0.5`) gain snow, exposed faces lose it. `fTerrainSnowCurvature = 0` is byte-identical to current behavior.

3. **Interaction with the existing AO exclusion** (`fTerrainSnowAmbientOcclusionExclusion`): unchanged and complementary — curvature *places* extra snow in dark-AO grooves, and the exclusion then keeps that snow from being AO-darkened, so groove snow reads bright/fresh rather than shadow-stained. The downstream `fAmbientOcclusionFactor` computation consumes the *modulated* `fSnowPercent`, which is the desired coupling.

### New uniform / slider

One `GlobalLayout` float `fTerrainSnowCurvature` after `fTerrainSnowAmbientOcclusionExclusion` (`ShaderLayoutsBase.h:438`); `Wrapper gTerrainSnowCurvature` in `TerrainWrappersBase.{h,cpp}` next to the two existing snow wrappers; "Snow Curvature" entry in the slider map and `WrapperSlider` call in `TweaksScreenTerrain.cpp` (next to "Snow AO Exclusion"); upload in `GlobalUniforms.cpp` next to the existing `fTerrainSnow*` assignments (~line 396). Suggested range [-1, 1], default 0 (negative flips the effect — snow on ridges — probably useless but free and occasionally illustrative while tuning).

## Critical files

- `Engine/Data/Shaders/Terrain/Terrain.frag` — AO fetch hoist + snow modulation block
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `fTerrainSnowCurvature`
- `Engine/Source/Ui/TerrainWrappersBase.{h,cpp}` — wrapper
- `Engine/Source/Ui/Screens/TweaksScreen/TweaksScreenTerrain.cpp` — slider map entry + `WrapperSlider`
- `Engine/Source/Graphics/Render/GlobalUniforms.cpp` — upload

## Out of scope

- Screen-space or normal-derivative curvature (the baked AO proxy is sufficient at RTS viewing distance and free; `fwidth`-based curvature is noisier and costs ALU)
- Any change to the Gaea mask authoring / DataPacker mask packing
- Seasonal / dynamic snow accumulation
- The `A` (Flow) mask channel (reserved for a future material)

## Notes

- Client-only rendering path; no determinism/CRC exposure; shader-only + one uniform — no `.pack`/`kiVersion` impact.
- Caveat: baked AO darkens large valleys as well as micro-grooves, so high slider values pull snow into whole valley floors. Largely desirable (snow collects in valleys); the band term caps how far it can deviate from the authored mask either way.
- Pre-staged decision for `/external-grill-plan`: whether the band term should be a slider-controlled exponent instead of the fixed `4·m·(1−m)` parabola — recommendation: keep fixed, one slider is enough for a modest effect.
