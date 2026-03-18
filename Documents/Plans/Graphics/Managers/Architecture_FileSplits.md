# Architecture: File Splits

Source: /external-architecture-review on Engine/Source/Graphics/Managers

## Changes

### Engine/Source/Graphics/Managers/CommandBufferManager.cpp (839 lines)
- Split into multiple .cpp files sharing CommandBufferManager.h (per engine pattern for struct splitting by responsibility):
  - **CommandBufferManager.cpp (core)**: Constructor/destructor, semaphore chain management, SubmitGlobalCommandBuffer, SubmitMainCommandBuffer, SubmitUiCommandBuffer, RecordCommandBuffers orchestration [~30m]
  - **CommandBufferManager_Lighting.cpp**: Lighting render passes, shadow compute/blur passes, object shadow passes [~30m]
  - **CommandBufferManager_Environment.cpp**: Terrain data passes, smoke/wind computation and rendering passes [~30m]
  - **CommandBufferManager_Objects.cpp**: Model rendering, particle passes, island rendering, visible lights, hex shields [~30m]
- This file is the coupling hub (references 8+ other managers); splitting by render subsystem isolates dependencies. Note: under the 1000-line mandatory split threshold — split is justified by coupling concerns, not line count [~2h total]

### Engine/Source/Graphics/Managers/PipelineManager.cpp (750 lines)
- Split into:
  - **PipelineManager.cpp (core)**: Constructor orchestration, shader loading, RecreatePipelineGroups, delegation to DynamicPipelines [~15m]
  - **PipelineManager_Static.cpp**: All CreateXxxPipelines() functions (CreateLightingPipelines, CreatePipelineShadows, CreateLightingShadowDependantPipelines, CreateTerrainDataPipelines, CreateSmokeWindPipelines, CreateParticlePipelines) [~30m]
- CreateSmokeWindPipelines is 147 lines and should be decomposed into smaller helpers during the split [~15m]

### Engine/Source/Graphics/Managers/BufferManager.cpp (733 lines)
- Split into:
  - **BufferManager.cpp (core)**: Constructor/destructor, CreateSwapchainDependentBuffers, DestroySwapchainDependentBuffers, dynamic buffer allocation (CreateDynamicBuffer, ResizeDynamicBuffer, GetDynamicStorageBuffer) [~15m]
  - **BufferManager_Mesh.cpp**: CreateTerrainMesh, CreateWaterMesh, AllocateMeshData, AllocateJointMatrices, ResetSkinningAllocations [~15m]
  - **BufferManager_Hierarchical.cpp**: Smoke/wind occupancy and active tile hierarchical buffer creation/destruction (lines 543-639) [~15m]

### Engine/Source/Graphics/Managers/InstanceManager.cpp (675 lines)
- Constructor is 470 lines. Extract logical sections into private helper methods:
  - ExtractValidationLayerSetup (lines 115-187) [~15m]
  - ExtractPhysicalDeviceSelection (lines 302-381) [~15m]
  - ExtractQueueFamilySelection (lines 440-493) [~10m]
  - ExtractFormatSelection (lines 495-577) [~10m]
- These remain in the same file but reduce constructor to ~100 lines of orchestration [~50m total]

### Engine/Source/Graphics/Managers/RenderTargetTextures.cpp (661 lines)
- CreateLightingTextures is 261 lines. Extract MRT attachment setup and blur mip chain into helpers [~30m]
- CreateTerrainTextures is 103 lines. Minimal — acceptable but monitor for growth [~0m]

## Verification Notes
- CommandBufferManager.cpp is under the 1000-line mandatory split threshold; split is driven by coupling, not line count alone
- InstanceManager.cpp: Physical device selection range corrected to 302-381 to include feature validation checks
- Do TechDebt_Duplication for BufferManager.cpp first — extracted helpers will define natural split boundaries
