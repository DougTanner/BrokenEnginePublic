# Architecture: Delete the Lighting Occupancy Grid (write-only, OOB hazard)

## Context
Source: /external-architecture-review on Engine/Source (recursive). The lighting deposit occupancy bitmask combines a floor-div CPU sizing with an unclamped shader tile write, producing a true GPU out-of-bounds `atomicOr` on non-multiple-of-8 deposit extents (`robustBufferAccess` is not enabled anywhere in Engine/Source) — and `Engine/Data/Shaders/Lighting/CLAUDE.md:11` documents the bitmask as write-only: no pass reads it (verified: `lightOccupancyBuffer` appears only in `AreaLight.frag`/`PointLight.frag`, both write-only `atomicOr`; the buffer is DEVICE_LOCAL with no TRANSFER_SRC and no CPU mapping — the Wind/Smoke occupancy systems with real dilate readers are separate buffers). Every deposit fragment pays an atomic for dead machinery with an OOB hazard attached.

**Not dead**: `uiLightTilesX/Y` (`ShaderLayoutsBase.h:310-311`). They double as the deposit-texture-size proxy for `LightingDepositEdgeFade` (`ShaderFunctions.h:257`), read by `AreaLight.frag:90`, `PointLight.frag:90`, and `Objects/HexShieldLighting.frag:61`. The uniforms and their writes stay.

## Design

**Decision (2026-07-03): delete (former Option A).** Resolved jointly with `Graphics/WindowedLightingShadowDispatch.md`: that plan windows the lighting/shadow passes via CPU-computed indirect dispatch counts (written from the uniform-populate path) and dynamic scissor — it never consumes GPU occupancy, and lists the deposit per-light rasterization as out of scope. No planned or existing reader exists (re-verified 2026-07-03: every CPU touch of `mLightOccupancyVkBuffers` is create/destroy/bind/clear; the only shader uses are write-only `atomicOr` in `AreaLight.frag`/`PointLight.frag`). Keeping it "for future hierarchical dispatch" is YAGNI; the ceil-div fix (former Option B) would preserve dead machinery plus a per-fragment atomic to fix an OOB in a buffer nothing reads.

Remove the occupancy machinery end to end; the edge-fade uniforms stay. [~1-2h]

1. Shaders: delete the `lightOccupancyBuffer` SSBO declaration (`AreaLight.frag:25-28`, `PointLight.frag:25-28`) and the `atomicOr` tile-write block (`AreaLight.frag:96-100`, `PointLight.frag:96-100`). Shader repack required.
2. Pipelines: remove the matching `{.flags = kStorageBuffer, .pVkBuffers = &gpBufferManager->mLightOccupancyVkBuffers[0]}` descriptor entries in `DynamicPipelines::CreatePipelineLighting` (`DynamicPipelines.cpp:172`) and `CreatePipelineAxisAlignedLighting` (`:230`) — a descriptor-set-layout change; the C++ entry and the GLSL binding must be removed together (remaining set-1 binding 1, the quads SSBO, is unaffected; the hex-shield lighting pipeline never bound occupancy and is untouched).
3. Command buffer: delete the per-frame occupancy clear and its two barriers in `CommandBufferRecordMain.cpp:50-78` (`vkOccupancyPreClearBarrier`, `vkCmdFillBuffer`, `vkOccupancyClearToFragmentBarrier`); drop the occupancy mention from the deposit→spread barrier comment at `:107` and the now-unneeded `VK_ACCESS_SHADER_WRITE_BIT` occupancy contribution folded into that barrier's `srcAccessMask` if nothing else needs it.
4. Buffers: delete `mLightOccupancyVkBuffers` / `mLightOccupancyVmaAllocations` / `mLightOccupancyBufferSizes` (`BufferManager.h:83-87`) and `CreateLightingSpreadBuffers`/`DestroyLightingSpreadBuffers` (`BufferManager.cpp:654-683`) with their call sites (`BufferManager.cpp:228,240`, `PipelineManager.cpp:86`).
5. Docs: update `Engine/Data/Shaders/Lighting/CLAUDE.md:11` (drop the occupancy sentence), `CommandBufferManager.CLAUDE.md` (occupancy clear step), and `BufferManager.CLAUDE.md:28` (delete the `mLightOccupancyVkBuffers[]` bullet).

### Evidence chain (why the OOB is real, for context)
- `TextureManager::LightingDetailTextureSize` (`TextureManager.cpp:34-47`) is `lround(base * kfLightingHeadroomMultiplier)` forced even-only (`iX &= ~1`), never 8-aligned — with the floor-div sizing (`BufferManager.cpp:660-661`) and the unclamped shader tile index (`uiTileY * uiLightTilesX + uiTileX`, `AreaLight.frag:97-100`), bottom-row partial-tile fragments index past the buffer end (modulo the ≤31-tile `/32` rounding slack); right-edge partials wrap into the wrong row. Deletion removes the hazard with the machinery.

## Critical files
- `Engine/Data/Shaders/Lighting/AreaLight.frag`, `PointLight.frag`, `Engine/Data/Shaders/Lighting/CLAUDE.md`
- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp`
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp`
- `Engine/Source/Graphics/Managers/BufferManager.{h,cpp}`, `PipelineManager.cpp`, `CommandBufferManager.CLAUDE.md`, `BufferManager.CLAUDE.md`

## Out of scope
- `uiLightTilesX/Y` and `LightingDepositEdgeFade` — live edge-fade machinery, untouched
- Windowed/indirect dispatch of the lighting passes (`Graphics/WindowedLightingShadowDispatch.md`)
- The shadow/lighting uniform-population dedup (`Graphics/Architecture_ShadowLightingUniformDedup.md`)
- Any other lighting shader change

## Notes
- Invariant exposure: client/graphics-only; no determinism/CRC/wire. Shader edit → repack required. Changes the two deposit pipelines' set-1 descriptor layouts (C++ and GLSL must move together in one repack); no `ShaderLayoutsBase.h` change.
- `LightingUniforms.cpp:70-72` (`uiLightTilesX/Y` writes and their "Light-occupancy tile grid" comment) stays — the uniforms feed `LightingDepositEdgeFade`; only reword the comment to say edge-fade tile grid, no math change.
- Joint decision with `Graphics/WindowedLightingShadowDispatch.md` resolved 2026-07-03 (see Design): that plan's windowing is CPU-driven (indirect dispatch counts + dynamic scissor), so it has no occupancy dependency and needs no text change. Both plans edit `CommandBufferRecordMain.cpp`; whichever lands second refreshes line citations as usual.

## Verification Notes
- Headline claim re-verified 2026-07-03: zero readers of the lighting occupancy SSBO across all shaders and CPU code (`lightOccupancyBuffer` only in `AreaLight.frag:25`/`PointLight.frag:25`, both write-only `atomicOr`; every CPU reference to `mLightOccupancyVkBuffers` is create/destroy/bind/clear); `robustBufferAccess` has zero hits in Engine/Source; `BufferManager.CLAUDE.md:28` documents the buffer as write-only.
- An earlier draft wrongly deleted the `uiLightTilesX/Y` uniform writes and `ShaderLayoutsBase.h` fields — they feed `LightingDepositEdgeFade` in three fragment shaders; corrected above.
