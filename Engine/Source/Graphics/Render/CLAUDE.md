# Render - GPU Uniform Buffer Population

Per-subsystem files that populate host-visible uniform buffers each frame before command buffer submission.

## Overview

Each file owns a specific region of the GPU uniform layout, reading from game state and UI settings to fill `GlobalLayout` or `MainLayout` uniform buffers indexed by command buffer slot. All files are client-only (`#ifdef BT_CLIENT`).

## Key Files

- **GlobalUniforms.cpp** - `RenderFrameGlobal`: camera, projection matrices, and shared global state. Delegates to four static helpers: `PopulateSunAndLighting`, `PopulateShadowParameters`, `PopulateTerrainParameters`, and `PopulateWaterParameters`. `PopulateSunAndLighting` computes both sun and moon direction/color — moon uses a flipped sun direction with a white/blue color scaled by nighttime intensity. Computes the stable lighting area (ceil'd dimensions, texel-grid-snapped origin) following the same pattern as smoke/wind areas. `PopulateTerrainParameters` lerps `gLightingDayFinalMultiplier` and `gLightingNightFinalMultiplier` by `fDayPercent` to produce the `fLightingTimeOfDayMultiplier` uniform. `PopulateWaterParameters` computes double-precision reduced UV offsets for water normal map and noise texture sampling (based on camera position and time), passing them as float uniforms to eliminate precision artifacts far from the world origin; also writes five debug offset fields from `WaterDebug` Wrapper globals for precision testing
- **MainUniforms.cpp** - `RenderFrameMain`: per-coordinate main render pass uniform population (lighting, skinning allocations, collection rendering). Also calls a static helper to debug-visualize NavData (obstacle polygons, visibility graph edges, and vertex markers) via `DebugRender` before `DebugRender::BeginRender`
- **LightingUniforms.cpp** - `RenderLightingGlobal` and `RenderLightingMain`: radial spread with start/end interpolation pairs (distance, ring count, jitter, decay, directionality, pass count), height-aware spread attenuation, per-ring rotation angles seeded from jitter, combine parameters (Uchimura tone curve parameters (max brightness, contrast, linear start/length, toe, black tightness), hue preserve), directional/ambient weights and power mode (luminance-vs-average blend for hue-preserving scalar pow), water height darken, water lighting (normal soften, wave blend, multi-term power, power mode), water ambient power mode, water skybox reflection parameters, PBR parameters, and smoke shadow intensity
- **SmokeUniforms.cpp** - Smoke simulation parameters and ping-pong index management, including smoke color, lighting multiplier, and decay uniforms
- **WindUniforms.cpp** - Wind simulation parameters, ping-pong index toggle, and wind spread quad offset. Computes `uiWindTilesX` from texture dimensions for use by the hierarchical indirect dispatch compute shaders.

## Architecture Notes

Uniform buffer structs (`GlobalLayout`, `MainLayout`) use scalar block layout (`GL_EXT_scalar_block_layout`) — do not add padding fields.

Uniform population is split by subsystem to keep files small and focused. `Render.h` declares shared state: `gbSmokeClear`/`gbWindClear` (initial-clear flags) and `giWindTextureIndex` (ping-pong index). Wind and smoke each manage their own timing and state, toggling once per frame in their respective Uniforms files. The wind spread storage buffer (camera offset quad) is also written here, matching the pattern used by smoke.

`WriteSpreadQuad` in `Render.h` is a shared inline helper used by both smoke and wind to compute the axis-aligned quad that offsets the spread texture for camera movement, keeping the two subsystems consistent.
