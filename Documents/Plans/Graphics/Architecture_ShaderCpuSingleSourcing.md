# Architecture: Shader/CPU Constant Single-Sourcing

## Context
Source: /external-architecture-review on Engine/Source (recursive). Three storage-image format families and two dispatch sites are hand-mirrored between C++ and GLSL instead of using the established `ke*Format` dual-language constant pattern (smoke/wind/elevation/lighting families already have it); three dead dual-language constants mislead readers. All currently match (verified pairwise) — the risk is a one-sided future edit producing a silent storage-image format mismatch (UB) or under-dispatch.

## Design

### Engine/Source/Graphics/Managers/TextureManager.cpp
- Replace the literal `(x + 7) / 8` divisors in the lighting-blur dispatches (`TextureManager.cpp:792,799`) with `shaders::kiComputeTileSize` — the shaders (`LightingBlurH.comp:5`, `LightingBlurV.comp:5`) declare `local_size = kiComputeTileSize`; every other dispatch site already uses the constant [~5m]
- Use `keElevationFormat` at the literal `VK_FORMAT_R16_SFLOAT` sites `TextureManager.cpp:125` and `IslandTerrainResidency.cpp:81` [~5m]

### Engine/Data/Shaders/ShaderLayoutsBase.h
- Delete the three dead dual-language constants `kiLightingTextures = 11`, `kiBillboardTexturesCount = 3` (:121-122) and `kiMaxAlphaMesh = 16` (:184) — zero references in shaders, Engine, game, DataPacker, or Common (repo-wide grep); readers will trust them as live sizing values [~5m]
- Add `keShadowFormat` (R16_UNORM), `keCombineFormat` (R8G8B8A8_UNORM), `keWaterDisplacementFormat` (R16G16B16A16_SFLOAT) following the existing `ke*Format` pattern, and reference them from: shadow chain `RenderTargetTextures.cpp:107-152,296-332` (GLSL `r16` qualifiers in `Shadow.comp:14`, `ShadowBlurH/V.comp:14`, `ShadowTemporal.comp:15`, `ObjectShadowsBlurH/V.comp:14`); combine/history `RenderTargetTexturesLighting.cpp:309-363`, `TextureManager.cpp:730,752` (`rgba8` in `LightCombine.comp:16-19`, `LightingTemporal.comp:19-22`, `LightingBlurH/V.comp:13`); water displacement (`rgba16f` in `WaterDisplacement.comp:19-20`) [~30m]

## Critical files
- `Engine/Data/Shaders/ShaderLayoutsBase.h`
- `Engine/Source/Graphics/Managers/TextureManager.cpp`, `RenderTargetTextures.cpp`, `RenderTargetTexturesLighting.cpp`
- `Engine/Source/Frame/IslandTerrainResidency.cpp`
- Shadow/Lighting/Water `.comp` shaders (comment-level references; GLSL format qualifiers stay literal with a pointer to the constant)

## Out of scope
- The lighting occupancy grid (`Graphics/Architecture_LightingOccupancyRemoval.md`)
- The `[256]` Gerstner wave arrays' missing shared count constant — water is in-flight this session; noted, not filed
- Any behavioral shader change

## Notes
- Invariant exposure: client/graphics-only; deleting the dead constants and adding new ones changes `ShaderLayoutsBase.h` → shader repack; no determinism/CRC/wire. `ShaderLayoutsBase.h` and `WaterDisplacement.comp` have in-flight water-session edits — refresh line cites at execution
- Grill decision: none material — mechanical once the `ke*Format` naming matches the existing family convention
