# Refactor: Dead Code Removal (Graphics/Managers)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). Unreferenced
declarations, write-only members, dead stores, and unreachable branches — each grep-verified by the review;
re-verify zero references at execution before deleting (line numbers refresh via `/next-plan`).

## Design

### Engine/Source/Graphics/Managers/DeviceManager.h
- `FindMemoryType` declaration (`DeviceManager.h:8`) — zero definitions and zero call sites repo-wide.
  Delete. [~5m]

### Engine/Source/Graphics/Managers/DeviceManager.cpp
- Dead store: `fragmentStoresAndAtomics = VK_TRUE` inside the GPU-AV constexpr block (`:206`) duplicates the
  unconditional assignment at `:194`. Delete `:206` (optionally fold `:194` into the designated initializer
  at `:180-193` — declaration order permits). [~5m]

### Engine/Source/Graphics/Managers/DynamicPipelines.h / .cpp
- `ModelPipelineSpec::name` (`DynamicPipelines.h:8`) is write-only — both construction sites set it
  (`DynamicPipelines.cpp:48, 92`) but `CreateModelPipeline` reads only
  `sceneCrc`/`pipelineInfo`/`bAddModelDescriptors`/`bIsPipelineShadow` (`:19-28`); the name actually used is
  `pipelineInfo.name`. Delete the member + the two initializers. [~5m]
- `ModelPipelineSpec::bAddModelDescriptors` (`DynamicPipelines.h:11`) is `true` at both construction sites
  (`:63, 109`) and `ModelPipeline::Create`'s only other caller is its own `Recreate()` replaying the stored
  value — the `false` path is dead end-to-end. Remove the field (and, where it stays a parameter chain into
  `ModelPipeline`, the parameter). If both remaining bools are kept instead, convert to
  `common::Flags<...>` per root CLAUDE.md. [~15m]

### Engine/Source/Graphics/Managers/InstanceManager.h / .cpp
- `mHinstance`/`mHwnd` members (`InstanceManager.h:73-74`) are read only inside the ctor (`:350-351`); use the
  ctor parameters directly and delete the members (`Graphics.h` keeps its own copies for recreate). [~5m]
- `ReadLayerProperties`: (a) the body-wide `if constexpr (kbVulkanDebugLayers)` (`:699`) duplicates the
  identical guard at the sole call site (`:121`) — drop the inner wrapper; (b) both `while (true)` loops
  (`:702-712, 742-752`) retry on `VK_INCOMPLETE` from *count-query* calls (null `pProperties`), which per
  spec cannot return `VK_INCOMPLETE` — flatten to plain `CHECK_VK` calls. [~15m]

### Engine/Source/Graphics/Managers/TextManager.cpp / .h
- Dead file-static `sVkMappedMemoryRange` (`TextManager.cpp:11-18`) — never read or passed (repo-wide grep);
  leftover of a removed manual-flush path (members part-commented-out). Delete. [~5m]
- `MeasureQuads` (`TextManager.h:101-126`) has **zero callers** repo-wide — and if revived as-is its
  `std::vector<float>` return is a per-call main-loop heap allocation. Delete per YAGNI. Then simplify
  `WriteQuads` (`TextManager.h:128-168`): the `std::span<const float> xOffsets` + `iCurrentX` ring exists
  solely to consume `MeasureQuads`' per-line widths; the sole caller (`TextManager.cpp:84`) always passes a
  1-element span manufactured through a workbuffer arena push of a single float (`TextManager.cpp:77-78`).
  Replace the span with `float fX`, dropping the per-character `std::min` clamp and the arena dance in
  `RenderMain`. (If the remaining 9 parameters warrant it, fold `fY/fSize/uiColor/fX/fYScreenOffset` into a
  small params struct.) [~30m]

### Engine/Source/Graphics/Managers/TextureManager.cpp — verify-first
- `ProcessPendingTextures`: `bFromTransferQueue = rLazyChunk.pData != nullptr` (`:627`) looks vestigial — the
  only writer of `kGpuUploadComplete` is `UploadThread` (`TextureUploadManager.cpp:404`), which requires
  non-null `pData`; the device-lost path stores `kDiskLoaded` (`:420`) instead. **Verify** no path presents
  `kGpuUploadComplete` with null `pData` (audit all stores of that state + `FileManager::
  ResetTextureChunkStates`); if confirmed, `bNeedsAcquire` reduces to `bNeedAcquireBarrier` and the
  `bFromTransferQueue &&` at `:669` is dead — delete the flag. [~15m]

### Engine/Source/Graphics/Managers/TextureCache.cpp
- `CopyImageToHostMemory`: `currentLayout`/`restoreLayout` (`:14-15`) computed from the identical ternary —
  one variable suffices; `iMipWidth`/`iMipHeight` re-inits at `:67-68` are dead stores (unconditionally
  reassigned at the top of the layer loop, `:73-74`). [~5m]

## Critical files
- `Engine/Source/Graphics/Managers/`: DeviceManager.h/.cpp, DynamicPipelines.h/.cpp, InstanceManager.h/.cpp,
  TextManager.h/.cpp, TextureManager.cpp, TextureCache.cpp

## Out of scope
- Unused *includes* — `Architecture_IncludeHygiene.md`.
- The dead `TerrainPipelineBindings` constants — `Architecture_ShaderCpuContractConstants.md` (delete-vs-wire
  decision).
- `DestroyTransferResources`' redundant double hash lookup (`TextureUploadManager.cpp:101-110`) — teardown-only
  indirection, below the value bar; noted and skipped.

## Acceptance criteria
- Every removed symbol has zero remaining references (grep-clean); client builds clean; the `WriteQuads`
  simplification renders the debug/profile text identically.

## Notes
- No determinism/CRC/`kiVersion` exposure — all client-only render/UI code, no serialized state.
- The `bFromTransferQueue` item is the only behavior-adjacent one (gated on its verification step); everything
  else is pure removal.

## Verification Notes

Verified against source 2026-06-11 (verification pass for the /external-deep-analysis run). All items
confirmed; no removals:

- **Zero-reference greps re-run**: `FindMemoryType` — declaration only (`DeviceManager.h:8`), no definition, no
  callers repo-wide. `sVkMappedMemoryRange` — `TextManager.cpp:11` only. `MeasureQuads` — declaration only
  (`TextManager.h:102`), zero callers. `CreateVisibleAreaMesh` — defined `BufferManager.cpp:645`, sole use at
  `:711` inside `static BuildLodConcatMesh` (`:683`).
- **`ModelPipelineSpec::name` write-only confirmed**: `CreateModelPipeline(const ModelPipelineSpec&)`
  (`DynamicPipelines.cpp:19-28`) reads only `sceneCrc`/`pipelineInfo`/`bAddModelDescriptors`/`bIsPipelineShadow`
  (`:22`); the two construction sites set `.name` at `:48` and `:92` (shadow path also passes `rShadowName` as
  `pipelineInfo.name`, which is the one actually used).
- **`bAddModelDescriptors`**: `true` at both construction sites (`:63, :109`); no other `ModelPipelineSpec`
  construction repo-wide.
- **`mHinstance`/`mHwnd`**: initialized in the ctor init list (`InstanceManager.cpp:112-113`), read only at
  `:350-351` (Win32 surface create) — same function.
- **`ReadLayerProperties`**: body-wide `if constexpr (kbVulkanDebugLayers)` at `:699` duplicating the call-site
  guard at `:119-122`; `while (true)` VK_INCOMPLETE-retry loops at `:702-712` and `:742-752`, both on
  count-queries (null pProperties) which per spec cannot return `VK_INCOMPLETE`.
- **`WriteQuads` simplification**: sole caller `TextManager.cpp:84` passes a 1-element span built by the
  workbuffer arena push of a single float at `:77-78`.
- **`bFromTransferQueue` (verify-first framing stands)**: `:627` flag, `:669` use; only `kGpuUploadComplete`
  writer is `UploadThread` (`TextureUploadManager.cpp:404`, requires non-null `pData`); device-lost path stores
  `kDiskLoaded` (`:420`); `FileManager::ResetTextureChunkStates` stores only `kNotLoaded`/`kDiskLoaded`. The
  execution-time audit step remains appropriate.
- **TextureCache dead stores**: `currentLayout`/`restoreLayout` identical ternaries at `:14-15`;
  `iMipWidth`/`iMipHeight` re-inits at `:67-68` dead (unconditionally reassigned at `:73-74`).
