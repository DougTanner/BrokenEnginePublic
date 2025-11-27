# Graphics System

Game-specific rendering pipeline configuration for BrokenEngineSandbox, managing all glTF model rendering for player, enemies, and projectiles.

## Overview

GltfPipelines manages Vulkan rendering pipelines for all 3D models in the game. It creates separate pipelines for shadow rendering and main scene rendering, with each pipeline configured for specific game objects using indirect drawing.

## GltfPipelines Class

Central manager for all glTF rendering pipelines in the game. Accessed via global pointer `gpGltfPipelines`.

**Constructor/Destructor** - Sets and clears the global pointer for singleton access pattern.

**CreateGltfShadowPipelines()** - Initializes shadow-specific rendering pipelines for player missiles. Shadow pipelines render to `mObjectShadowsTexture` render target with specialized shadow fragment shader. All shadow pipelines use indirect drawing with host-visible buffers and allocate secondary command buffers for parallel rendering.

**CreateGltfPipelines()** - Initializes main scene rendering pipelines for player missiles. Main pipelines include depth testing/writing, back-face culling, and sample shading. Player and Spaceships pipeline creation handled separately through Frame::CreatePipelines() → PlayerInterpolate::CreatePipelines() and SpaceshipsInterpolate::CreatePipelines().

**RecordGltfPipelines()** - Legacy method for recording static test pipelines. Most glTF pipelines now use secondary command buffers and record via callbacks during command buffer manager iteration.

## Pipeline Configuration

Each pipeline is configured with:
- Specific glTF model data (player spaceship, missile, enemy spaceship)
- Vertex and fragment shaders from engine shader manager
- Descriptor sets for global uniforms, per-frame uniforms, and per-object storage buffers
- Pipeline flags for rendering features (depth test, culling, sample shading, indirect drawing)
- Push constants for per-draw data

## Pipeline Enum

**GltfPipelinesEnum** defines indices for all pipeline types:
- Player missiles (main and shadow)
- Player spaceship (main and shadow)
- Enemy spaceships (main and shadow)
- Optional test pipeline for development

## Storage Buffer Binding

Each pipeline type registers and binds storage buffers dynamically via BufferManager::CreateBuffer():
- Player missiles - Registered in GltfPipelines, uses mPlayerMissilesStorageBuffers
- Player spaceship - Registered in PlayerInterpolate::CreatePipelines()
- Enemy spaceships - Registered in SpaceshipsInterpolate::CreatePipelines()
- Test objects - Uses mGltfsStorageBuffers (debug only)

## Rendering Architecture

Follows the engine's indirect drawing pattern where game logic updates storage buffers with instance data, and rendering uses indirect draw calls to efficiently render multiple instances. Shadow passes render from different perspective for shadow map generation using secondary command buffers with vertical offset push constant (0.0, 2.0, 0.0, 0.0). Main passes render from camera perspective with full lighting. CommandBufferManager iterates shadow pipelines via mDynamicGltfPipelineShadowMap for automatic secondary buffer execution.

## Dynamic Buffer Resizing

**Purpose**: Allows storage buffers to grow at runtime when object collections exceed initial capacity.

**Implementation**:
- Collections detect capacity overflow during rendering
- BufferManager::ResizeDynamicBuffer() creates larger buffer and updates registry
- GltfPipeline::UpdateStorageBufferDescriptors() updates all material pipeline descriptors
- Uses VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT for descriptor updates without command buffer re-recording
- No pipeline recreation or frame stalls required
