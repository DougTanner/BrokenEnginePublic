# PipelineManager

**Global**: `gpPipelineManager`

Loads all SPIR-V shader modules from pack chunks at construction and creates all graphics and compute pipelines (~60 static plus dynamic per-collection). Shaders are stored in a CRC-keyed `mShaders` map and referenced by pipelines via pointer.

## Static Pipelines

Indexed by `Pipelines` enum, covering lighting (MRT R/G/B blur chains), shadows, terrain, water, particles, smoke, and UI.

## Dynamic Pipelines (`mDynamicPipelines`)

Delegated to the `DynamicPipelines` sub-object (`DynamicPipelines.h/.cpp`), owned as a member of PipelineManager. Collections register pipelines during CreatePipelines() phase via `gpPipelineManager->mDynamicPipelines.Create*()`. Two indexing systems: `DynamicPipelineType` for non-model pipelines (lighting, visible lights, billboards, smoke, wind deposit, hex shields) and `DynamicModelPipelineType` for model pipelines (regular and shadow). Both stored as CRC-keyed maps accessed via `mDynamicPipelines.mPipelineMaps` and `mDynamicPipelines.mModelPipelineMaps`.

`ModelPipelineSpec` configures model pipeline creation with scene CRC, pipeline info, and flags for model descriptors and shadow mode. DynamicPipelines holds a reference to PipelineManager's shader map for shader lookups during pipeline creation. All Create* implementations are client-only (`#ifdef BT_CLIENT`).

## Selective Recreation

`RecreatePipelineGroups()` rebuilds only pipeline groups affected by specific resource changes via DestroyFlags, respecting dependency ordering. Falls back to full rebuild when necessary.
