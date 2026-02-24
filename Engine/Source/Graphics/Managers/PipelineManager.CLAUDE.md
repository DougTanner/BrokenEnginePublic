# PipelineManager

**Global**: `gpPipelineManager`

Loads all SPIR-V shader modules from pack chunks at construction and creates all graphics and compute pipelines (~60 static plus dynamic per-collection). Shaders are stored in a CRC-keyed `mShaders` map and referenced by pipelines via pointer.

## Static Pipelines

Indexed by `Pipelines` enum, covering lighting (MRT R/G/B blur chains), shadows, terrain, water, particles, smoke, and UI.

## Dynamic Pipelines

Collections register pipelines during CreatePipelines() phase. Two indexing systems: `DynamicPipelineType` for non-model pipelines (lighting, visible lights, billboards, smoke, wind deposit, hex shields) and `DynamicModelPipelineType` for model pipelines (regular and shadow). Both stored as CRC-keyed maps.

## Model Pipelines

Auto-selects skinned vs. static vertex shader based on animation flag from scene header. Regular pipelines use 3-set descriptor layout (Set 0 global, Set 1 shared, Set 2 per-material). Shadow pipelines use minimal descriptors and skip transparency overrides.

## Selective Recreation

`RecreatePipelineGroups()` rebuilds only pipeline groups affected by specific resource changes via DestroyFlags, respecting dependency ordering. Falls back to full rebuild when necessary.
