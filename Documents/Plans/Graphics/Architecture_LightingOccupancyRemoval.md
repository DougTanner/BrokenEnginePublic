# Architecture: Lighting Occupancy Grid — Delete or Fix OOB Write

## Context
Source: /external-architecture-review on Engine/Source (recursive). The lighting deposit occupancy bitmask combines a floor-div CPU sizing with an unclamped shader tile write, producing a true GPU out-of-bounds `atomicOr` on non-multiple-of-8 deposit extents (`robustBufferAccess` is not enabled anywhere in Engine/Source) — and `Engine/Data/Shaders/Lighting/CLAUDE.md:11` documents the bitmask as write-only: no pass reads it (verified: `lightOccupancyBuffer` appears only in `AreaLight.frag`/`PointLight.frag`, both write-only `atomicOr`; the buffer is DEVICE_LOCAL with no TRANSFER_SRC and no CPU mapping — the Wind/Smoke occupancy systems with real dilate readers are separate buffers). Every deposit fragment pays an atomic for dead machinery with an OOB hazard attached.

**Not dead**: `uiLightTilesX/Y` (`ShaderLayoutsBase.h:305-306`). They double as the deposit-texture-size proxy for `LightingDepositEdgeFade` (`ShaderFunctions.h:257`), read by `AreaLight.frag:90`, `PointLight.frag:90`, and `Objects/HexShieldLighting.frag:61`. The uniforms and their writes stay under either option below.

## Design

### Decision: delete (recommended) or align sizing
- **Option A — delete (KISS, recommended):** remove the occupancy machinery end to end; the edge-fade uniforms stay. [~1-2h]
  - Shaders: delete the `lightOccupancyBuffer` SSBO declaration (`AreaLight.frag:25-28`, `PointLight.frag:25-28`) and the `atomicOr` tile-write block (`AreaLight.frag:96-100`, `PointLight.frag:96-100`). Shader repack required.
  - Pipelines: remove the matching `{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mLightOccupancyVkBuffers[0]}` descriptor entries in `DynamicPipelines::CreatePipelineLighting` (`DynamicPipelines.cpp:172`) and `CreatePipelineAxisAlignedLighting` (`:230`) — a descriptor-set-layout change; the C++ entry and the GLSL binding must be removed together (remaining set-1 binding 1, the quads SSBO, is unaffected).
  - Command buffer: delete the per-frame occupancy clear and its two barriers in `CommandBufferRecordMain.cpp:47-75` (`vkOccupancyPreClearBarrier`, `vkCmdFillBuffer`, `vkOccupancyClearToFragmentBarrier`); drop the occupancy mention from the deposit→spread barrier comment at `:104` (and any occupancy access bits folded into that barrier).
  - Buffers: delete `mLightOccupancyVkBuffers` / `mLightOccupancyVmaAllocations` / `mLightOccupancyBufferSizes` (`BufferManager.h:83-87`) and `CreateLightingSpreadBuffers`/`DestroyLightingSpreadBuffers` (`BufferManager.cpp:654-683`) with their call sites (`BufferManager.cpp:228,240`, `PipelineManager.cpp:86`).
  - Docs: update `Engine/Data/Shaders/Lighting/CLAUDE.md:11` (drop the occupancy sentence) and `CommandBufferManager.CLAUDE.md:16` (occupancy clear step).
- **Option B — keep and fix:** ceil-div both CPU sites to match the smoke/wind convention (`BufferManager.cpp:660-661` and `LightingUniforms.cpp:71-72`; smoke/wind ceil at `BufferManager.cpp:572-573`/`:612-613`, `SmokeUniforms.cpp:37-38`, `WindUniforms.cpp:54-55`) and clamp the shader-side `uiTileX/Y` to `uiLightTilesX/Y - 1` — only if occupancy data is wanted for future Wind/Smoke-style hierarchical dispatch. Ceil also nudges the `LightingDepositEdgeFade` denominator (tiles×8 ≥ extent instead of ≤) by under one tile — visually negligible but note it. [~30m]

### Evidence chain (for the implementer)
- `TextureManager::LightingDetailTextureSize` (`TextureManager.cpp:34-47`) is `lround(base * kfLightingHeadroomMultiplier)` forced even-only (`iX &= ~1`), never 8-aligned — with the floor-div sizing (`BufferManager.cpp:660-661`) and the unclamped shader tile index (`uiTileY * uiLightTilesX + uiTileX`, `AreaLight.frag:97-100`), bottom-row partial-tile fragments index past the buffer end (modulo the ≤31-tile `/32` rounding slack); right-edge partials wrap into the wrong row.

## Critical files
- `Engine/Data/Shaders/Lighting/AreaLight.frag`, `PointLight.frag`, `Engine/Data/Shaders/Lighting/CLAUDE.md`
- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp`
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`
- `Engine/Source/Graphics/Managers/BufferManager.{h,cpp}`, `PipelineManager.cpp`, `CommandBufferManager.CLAUDE.md`
- Option B only: `Engine/Source/Graphics/Render/LightingUniforms.cpp`

## Out of scope
- `uiLightTilesX/Y` and `LightingDepositEdgeFade` — live edge-fade machinery, untouched
- Windowed/indirect dispatch of the lighting passes (`Graphics/WindowedLightingShadowDispatch.md`)
- The shadow/lighting uniform-population dedup (`Graphics/Architecture_ShadowLightingUniformDedup.md`)
- Any other lighting shader change

## Notes
- Invariant exposure: client/graphics-only; no determinism/CRC/wire. Shader edit → repack required. Option A changes the two deposit pipelines' set-1 descriptor layouts (C++ and GLSL must move together in one repack); no `ShaderLayoutsBase.h` change under either option.
- Grill decision: A vs B — `WindowedLightingShadowDispatch` as planned windows via CPU-computed indirect dispatch and dynamic scissor, not occupancy-driven dispatch, so it does not need this buffer; still confirm before deleting, ideally in the session that plan is groomed.

## Verification Notes
- Headline claim re-verified 2026-07-02: zero readers of the lighting occupancy SSBO across all shaders and CPU code; `Lighting/CLAUDE.md:11` states it explicitly.
- Original Option A wrongly deleted the `uiLightTilesX/Y` uniform writes and `ShaderLayoutsBase.h` fields — they feed `LightingDepositEdgeFade` in three fragment shaders; corrected above.
