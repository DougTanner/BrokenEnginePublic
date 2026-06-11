# Refactor: Record & Swapchain Decomposition (Graphics/Managers)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). Record-once and
creation-path functions with duplicated halves or extractable phases. All non-hot paths (record happens on
boot/resize/device-loss/settings only); value is drift-resistance between duplicated halves and readability.

## Design

### Engine/Source/Graphics/Managers/SwapchainManager.cpp
- Ctor (`SwapchainManager.cpp:9-391`, 383 lines, four separable phases): render pass (`:18-127`), surface
  caps / present mode / extent / swapchain (`:129-257`), image views + framebuffers (`:259-362`), sync
  objects (`:364-390`). Unlike declarative RTT lists this mixes querying, clamping, selection loops, and
  creation; `ImGuiManager` in the same directory already models the decomposition
  (`CreateRenderPass`/`CreateFramebuffers`, `ImGuiManager.cpp:157/219`). Extract the four private helpers.
  Incidental while in there: `:235` casts `uint32_t` to `uint32_t` — drop. [~1h]

### Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp
- `CreateLightingTextures` (~380 lines): keep the declarative texture-creation bulk (`:64-402`); extract the
  debug-texture registry (`:404-435`) as `RegisterDebugTextures()` — a logically distinct concern with
  fragile slot arithmetic (`kiTerrainDebugSlotCount + 2 + i`, pass-count-dependent tail at `:422-424`;
  verified no overflow today: max index 43 < `kiMaxDebugTextures` 56). The extraction also fixes the braceless
  `if`s at `:429-431, 434-435`. [~30m]

### Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp
- `RecordSmokeSpreadPipeline` (`:269-445`, ~176 lines): the Dilate→Fill→Clear→SpreadIndirect sequence is
  written out twice (`:276-357` B-half vs `:359-442` A-half), differing only in dilate pipeline, cleared
  texture, spread pipeline, and the inter-half barrier — a parameterized local helper halves the function and
  removes drift risk between the halves. [~30m]
- `Record` (`:35-68`): the shadow chain is the only pass still inline (terrain/wind/smoke/particles have
  helpers) — extract `RecordShadowPasses` for symmetry, and hoist the 8×-repeated ceil-div
  `(uiShadowWidth + shaders::kiComputeTileSize - 1) / kiComputeTileSize` (`:41,44,47,60`) into
  `uiShadowGroupsX/Y` locals. [~15m]

### Engine/Source/Graphics/Managers/ImGuiManager.cpp
- Ctor (`:22-136`, 115 lines): the UI-prepass indirect-buffer block (`:30-54`) is already a self-contained
  anonymous scope — extract `CreateUiPrepassIndirectBuffer()`, bringing the ctor under 100 lines in line with
  the file's existing helper pattern. [~15m]

### Engine/Source/Graphics/Managers/BufferManager.cpp
- Ctor (`:12-216`, ~205 lines): the `kbDebugRender` block (`:37-164`) builds four debug meshes with the same
  create-lambda shape repeated 5× (including the quad at `:20-35`) — extract `CreateDebugMeshBuffers()` (+
  optionally a small shared index+vertex create helper). [~30m]

### Optional
- `CommandBufferRecordMain::Record` (`:16-328`, ~312 lines): pass-sequencing length is expected, but each pass
  is delimited by `GpuStart/GpuStop` pairs; the minimum worthwhile split is the lighting deposit (`:44-101`)
  and the swapchain main pass (`:209-325`), mirroring the file's own `RecordLightingSpreadPipeline` pattern.
  Take only if the file is being touched anyway (KISS/YAGNI). [~1h, optional]

## Critical files
- `Engine/Source/Graphics/Managers/SwapchainManager.cpp` (+ `.h` for helper declarations)
- `Engine/Source/Graphics/Managers/RenderTargetTexturesLighting.cpp` (+ `RenderTargetTextures.h`)
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp` (+ `.h`)
- `Engine/Source/Graphics/Managers/ImGuiManager.cpp` (+ `.h`)
- `Engine/Source/Graphics/Managers/BufferManager.cpp` (+ `.h`)
- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` (optional item)

## Out of scope
- `RenderTargetTextures.cpp` / `RenderTargetTexturesWaterSkyboxOne.cpp` `Create*` bodies — exactly the
  sanctioned long declarative sequences; cleared by the review, do not split.
- The smoke A/B *content* (pipelines, barriers) — only the duplication structure changes; recorded commands
  must be byte-identical.
- Behavior changes of any kind.

## Acceptance criteria
- Client builds clean; recorded command buffers produce identical rendering (the smoke A/B helper records the
  same sequence); no touched function exceeds ~100 lines except the declarative remainders.

## Notes
- No determinism/CRC exposure — record-once/creation paths only.
- Shares `SwapchainManager.cpp`/`BufferManager.cpp`/`CommandBufferRecordMain.cpp` with
  `Refactor_CorrectnessHardenings.md`, `Refactor_BufferGrowthAndBounds.md`, and `Refactor_MicroCleanups.md` —
  co-schedule (File Groups).
