# Refactor: Micro Cleanups (Graphics/Managers)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). Small mechanical
consistency items — valueless indirection, drift-prone manual mirrors, misfiled code, and brace/type nits that
sit next to hazards. None changes behavior.

## Design

### Valueless indirection through own `gp*` global
- `DeviceManager.cpp:316` (ctor) and `:406` (dtor) use `gpDeviceManager->mVkDevice` instead of plain
  `mVkDevice` — valid today (global assigned at `:10`, nulled at `:413` after use) but one reorder away from a
  null deref, and inconsistent with adjacent lines `:408-411`. Use the member. [~5m]
- `CommandBufferManager.cpp:131` (`SubmitMainToQueue`) accesses
  `gpCommandBufferManager->mPerFramebufferCommandBuffers` inside a non-static member function; sibling
  `SubmitGlobalToQueue` (`:71`) uses the member directly. Use the member. [~5m]
- `TextManager.cpp:45` — ctor calls `gpTextManager->UpdateTextArea(...)` (global assigned to `this` at `:22`);
  call `UpdateTextArea` directly. The call itself is live (clears the header-inline `gpTextAreas` slot across
  device-loss recreation) — keep it, drop the indirection. [~5m]

### Drift-prone manual mirrors
- `CommandBufferRecordMain.cpp:76/84` and `:358/366` — clear-value array sizes (`[3]`, `[6]`) manually
  mirrored into `.clearValueCount`; use `static_cast<uint32_t>(std::size(...))` as the file already does for
  barrier arrays. [~5m]
- `RenderTargetTexturesLighting.cpp:88-126` — three identical `VkAttachmentDescription` literals; the spread
  render pass in the same file already uses the loop form for the same job (`:237-254`). Collapse. [~15m]
- `CommandBufferRecordMain.cpp:342,385,422` — stale phase numbering ("Phase 1" → "Phase 3" → "Phase 4", no
  Phase 2); renumber or use pass names. Comment-only. [~5m]

### Misfiled / over-abstracted
- `PipelineManager.cpp:273-286` — `kPipelineWaterDisplacement` (a water compute pre-pass) is created inside
  `CreatePipelineShadows`; move it to `CreateLightingShadowDependentPipelines` where readers audit water
  pipelines (no ordering dependency — consumers are created later in the ctor sequence). [~5m]
- `PipelineManager.cpp:498-537` — `CreateTerrainDataPipelines`: a desc struct + 1-element array + range-for
  creating a single pipeline. Flatten to one direct `.Create(...)` call, preserving the kMax comments. [~15m]
- `DeviceManager.cpp:250-267` — `if (transfer == graphics)` tested twice back-to-back (queue fetch, then
  logging); merge the LOGs into the first if/else. [~5m]
- `BufferManager.cpp:400-405` — `CreateDynamicBuffer`: `contains()` + `at()` + `try_emplace` = three hash
  lookups; a single `try_emplace` using its `bInserted` result suffices. [~5m]

### Type/linkage consistency
- `TextureManager.h:79-111` — `smPriorityTextures` is a `static inline std::vector<common::crc_t>` for a
  compile-time-constant list (heap-constructs at static init, mutable global); sole consumer
  `RequestChunkLoad(std::span<const common::crc_t>, ...)` (`TextureManager.cpp:348`) accepts a C array (same
  pattern as `kpWaterNormalCrcs` at `:46`). Make it `static inline constexpr common::crc_t[]`. [~5m]
- `TextureDescriptors.cpp:397-413` — `CrcToIndex` returns `float` (shader-facing) but internal consumers
  round-trip it back to an index (`TextureDescriptors.cpp:302`, `TextureManager.cpp:909`). Exact below 2^24 so
  not a live bug; return `int64_t` and cast to float only at the shader-upload sites. [~15m]
- `BufferManager.cpp:645` — `CreateVisibleAreaMesh` is a header-undeclared free function with external
  linkage, used only by the `static` `BuildLodConcatMesh` (`:683,711`); make it `static`. [~5m]
- `BufferManager.cpp:343-344` — `XMFLOAT4X4 identity {}` + `XMStoreFloat4x4`; prefer aligned
  `XMFLOAT4X4A`/`XMStoreFloat4x4A` (root CLAUDE.md aligned-types rule). Boot path, cosmetic-perf. [~5m]
- Braceless single-line `if`s hiding in otherwise-braced files (style rule 2):
  `TextureDescriptors.cpp:420-421` (`CrcToBlurredIndex`) and `TextureUploadManager.cpp:157, 167, 314`. Add
  braces. [~5m]

## Critical files
- `Engine/Source/Graphics/Managers/`: DeviceManager.cpp, CommandBufferManager.cpp, TextManager.cpp,
  CommandBufferRecordMain.cpp, RenderTargetTexturesLighting.cpp, PipelineManager.cpp, BufferManager.cpp,
  TextureManager.h, TextureDescriptors.cpp, TextureUploadManager.cpp

## Out of scope
- The `CrcToBlurredIndex` silent fallback behavior — documented-intended (placeholder until blur adoption);
  only the braces are touched here. The `kBlurSalt` duplication that could silently break the fallback is
  `Architecture_ShaderCpuContractConstants.md`.
- `ImGuiManager.h:57` / `TextManager.h:163` `XMFLOAT4` members — reviewed and deliberately left: both are
  memcpy'd into GPU-layout buffers where `alignas` changes are hazardous and buy nothing.
- Function decompositions — the `Refactor_*Decomposition` plans.

## Acceptance criteria
- Client builds clean; rendering identical (all items are mechanical equivalences).

## Notes
- No determinism/CRC exposure. Pure consistency sweep; land any subset.
