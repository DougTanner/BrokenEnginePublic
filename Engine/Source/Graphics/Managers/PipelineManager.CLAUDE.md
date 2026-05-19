# PipelineManager

**Global**: `gpPipelineManager`

Loads all SPIR-V shader modules from pack chunks at construction and creates all graphics and compute pipelines (~60 static plus dynamic per-collection). Shaders are stored in a CRC-keyed `mShaders` map and referenced by pipelines via pointer.

## Static Pipelines

Indexed by `Pipelines` enum, covering shadows, terrain, water, particles, smoke, wind, and a debug texture visualization pipeline (debug builds only, gated on `kbDebugInput`). Smoke and wind spread pipelines are compute pipelines; smoke clear pipelines (`kPipelineSmokeClearA`/`kPipelineSmokeClearB`) remain fragment-shader render-pass pipelines. Wind pipelines include `kPipelineWindOccupancyDilateA`/`B` (compact pass) and `kPipelineWindSpreadComputeA`/`B` (spread pass).

## Lighting Pipelines

Lighting pipelines are stored as named members rather than in the `Pipelines` enum: an array of spread pipelines (fragment MRT, one per spread pass, each writing all three spread textures; each spread pipeline binds the terrain elevation texture as an additional sampler for height-aware attenuation), and a combine pipeline (compute, tone maps all three channels in one dispatch, reading from all spread pass outputs).

Two static compute pipelines (`kPipelineLightingBlurH`, `kPipelineLightingBlurV`) implement separable Gaussian blur for pre-blurring light type textures at load time. Their descriptors are updated per-texture before each blur dispatch via `UpdateStorageImageDescriptor()` and `UpdateCombinedImageSamplerDescriptor()`.

## Dynamic Pipelines (`mDynamicPipelines`)

Delegated to the `DynamicPipelines` sub-object (`DynamicPipelines.h/.cpp`), owned as a member of PipelineManager. Collections register pipelines during CreatePipelines() phase via `gpPipelineManager->mDynamicPipelines.Create*()`. Two indexing systems: `DynamicPipelineType` for non-model pipelines (lighting, visible lights, billboards, smoke, wind deposit, hex shields) and `DynamicModelPipelineType` for model pipelines (regular and shadow). Both stored as CRC-keyed maps accessed via `mDynamicPipelines.mPipelineMaps` and `mDynamicPipelines.mModelPipelineMaps`.

`ModelPipelineSpec` configures model pipeline creation with scene CRC, pipeline info, and flags for model descriptors and shadow mode. DynamicPipelines holds a reference to PipelineManager's shader map for shader lookups during pipeline creation. All Create* implementations are client-only (`#ifdef BT_CLIENT`).

## Recreation

Pipeline-tier destroy events (`Graphics::Destroy` with `meDestroyType >= kPipelines`) call `mpPipelineManager.reset()` and re-run the constructor. Static pipelines rebuild directly; dynamic pipelines repopulate lazily as collections issue their per-frame render code (idempotent `mPipelineMaps[type].contains(crc)` checks). Texture recreation stays selective (driven by `DestroyFlags`); only pipeline recreation is unconditional. Descriptor staleness is caught at the top of `CommandBufferRecord*::Record` by `VerifyAllDescriptorGenerations()`.
