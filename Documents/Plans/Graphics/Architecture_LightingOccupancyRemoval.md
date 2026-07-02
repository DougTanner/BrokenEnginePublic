# Architecture: Lighting Occupancy Grid — Delete or Fix OOB Write

## Context
Source: /external-architecture-review on Engine/Source (recursive). The lighting deposit occupancy bitmask combines a floor-div CPU sizing with an unclamped shader tile write, producing a true GPU out-of-bounds `atomicOr` on non-multiple-of-8 deposit extents (`robustBufferAccess` is not enabled) — and `Engine/Data/Shaders/Lighting/CLAUDE.md:11` documents the bitmask as write-only: no pass reads it. Every deposit fragment pays an atomic for dead machinery with an OOB hazard attached.

## Design

### Decision: delete (recommended) or align sizing
- **Option A — delete (KISS, recommended):** remove the `LightOccupancy` SSBO creation/sizing (`BufferManager.cpp:659-666`), the `uiLightTilesX/Y` uniform writes (`LightingUniforms.cpp:71-72`), and the `atomicOr` tile-write blocks in `Engine/Data/Shaders/Lighting/AreaLight.frag:97-100` and `PointLight.frag:97-100` (plus their SSBO/layout declarations); shader repack [~1h]
- **Option B — keep and fix:** ceil-div both CPU sites to match the smoke/wind convention (`BufferManager.cpp:572-573`, `:612-613` use ceil) and clamp the shader-side `uiTileX/Y` to the tile grid — only if `Graphics/WindowedLightingShadowDispatch.md` wants the occupancy data for future hierarchical dispatch [~30m]

### Evidence chain (for the implementer)
- `TextureManager::LightingDetailTextureSize` (`TextureManager.cpp:34-47`) is `lround(base * kfLightingHeadroomMultiplier)` forced even-only (`iX &= ~1`), never 8-aligned — a non-multiple-of-8 deposit extent puts bottom-row partial-tile fragments past the buffer end; right-edge partials wrap into the wrong row

## Critical files
- `Engine/Source/Graphics/Render/LightingUniforms.cpp`
- `Engine/Source/Graphics/Managers/BufferManager.cpp`
- `Engine/Data/Shaders/Lighting/AreaLight.frag`, `PointLight.frag`, `Engine/Data/Shaders/Lighting/CLAUDE.md`
- `Engine/Data/Shaders/ShaderLayoutsBase.h` (occupancy layout fields, if deleted)

## Out of scope
- Windowed/indirect dispatch of the lighting passes (`Graphics/WindowedLightingShadowDispatch.md`)
- The shadow/lighting uniform-population dedup (`Graphics/Architecture_ShadowLightingUniformDedup.md`)
- Any other lighting shader change

## Notes
- Invariant exposure: client/graphics-only; no determinism/CRC/wire. Shader edit → repack required. Deleting layout fields changes the UBO/SSBO layout — coordinate with any in-flight `ShaderLayoutsBase.h` edits (water session churn)
- Grill decision: A vs B — resolve against `WindowedLightingShadowDispatch`'s plans for hierarchical dispatch before deleting; decide in the same session that plan is groomed
