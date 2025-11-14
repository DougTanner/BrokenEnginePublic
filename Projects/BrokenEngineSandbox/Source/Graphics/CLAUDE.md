# Graphics System

Game-specific rendering pipeline configuration for BrokenEngineSandbox, managing all glTF model rendering for player, enemies, and projectiles.

## Overview

GltfPipelines manages Vulkan rendering pipelines for all 3D models in the game. It creates separate pipelines for shadow rendering and main scene rendering, with each pipeline configured for specific game objects using indirect drawing.

## GltfPipelines Class

Central manager for all glTF rendering pipelines in the game. Accessed via global pointer `gpGltfPipelines`.

**Constructor/Destructor** - Sets and clears the global pointer for singleton access pattern.

**CreateGltfShadowPipelines()** - Initializes shadow-specific rendering pipelines for player missiles, player spaceship, and enemy spaceships. Shadow pipelines render to `mObjectShadowsTexture` render target with specialized shadow fragment shader. All shadow pipelines use indirect drawing with host-visible buffers.

**CreateGltfPipelines()** - Initializes main scene rendering pipelines for player, player missiles, and enemy spaceships. Main pipelines include depth testing/writing, back-face culling, and sample shading. Each pipeline binds to appropriate storage buffers for instanced rendering.

**RecordGltfShadowPipelines()** - Records shadow rendering commands into provided command buffer. Applies vertical offset (0.0, 2.0, 0.0, 0.0) push constant for shadow positioning.

**RecordGltfPipelines()** - Records main scene rendering commands into provided command buffer for all visible game objects.

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

Each pipeline type binds to specific storage buffers for instanced data:
- `mPlayerMissilesStorageBuffers` - Missile transforms and state
- `mPlayerStorageBuffers` - Player spaceship transform
- `mSpaceshipsStorageBuffers` - Enemy spaceship transforms and state
- `mGltfsStorageBuffers` - Test objects (debug only)

## Rendering Architecture

Follows the engine's indirect drawing pattern where game logic updates storage buffers with instance data, and rendering uses indirect draw calls to efficiently render multiple instances. Shadow passes render from different perspective for shadow map generation, while main passes render from camera perspective with full lighting.
