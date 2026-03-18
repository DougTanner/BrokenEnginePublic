# Architecture: BufferManager Feature Envy

Source: /external-architecture-review on Engine/Source/Graphics/Managers

## Changes

### Engine/Source/Graphics/Managers/BufferManager.cpp
- Lines 505-512 and 533-540: BufferManager reaches into gpPipelineManager->mDynamicPipelines to update pipeline descriptors after mesh data buffer growth. This is feature envy — BufferManager should not know about pipeline descriptor internals [~30m]
- Refactor to a push/callback pattern: BufferManager notifies PipelineManager that a buffer was resized, and PipelineManager handles its own descriptor update internally
- Alternative: Move the descriptor update call into DynamicPipelines via a public method like OnBufferResized() that BufferManager calls, keeping the coupling surface minimal
- Also: binding indices 15 and 16 are hardcoded magic numbers — name them as constants during the refactoring

## Verification Notes
- Feature envy confirmed at lines 504-512 (GrowMeshDataBuffer) and 531-540 (GrowJointMatrixBuffer)
- BufferManager iterates over gpPipelineManager->mDynamicPipelines.mModelPipelineMaps and calls UpdateStorageBufferDescriptors with hardcoded binding indices
