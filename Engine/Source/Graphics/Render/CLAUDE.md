# Render - GPU Uniform Buffer Population

Per-subsystem files that populate host-visible uniform buffers each frame before command buffer submission.

## Overview

Each file owns a specific region of the GPU uniform layout, reading from game state and UI settings to fill `GlobalLayout` or `MainLayout` uniform buffers indexed by command buffer slot. All files are client-only (`#ifdef BT_CLIENT`).

## Key Files

- **GlobalUniforms.cpp** - Camera, projection matrices, and shared global state
- **MainUniforms.cpp** - Main render pass parameters
- **LightingUniforms.cpp** - Directional and ambient lighting parameters
- **SmokeUniforms.cpp** - Smoke simulation parameters and ping-pong index management
- **WindUniforms.cpp** - Wind simulation parameters, ping-pong index toggle, and wind spread quad offset. Computes `uiWindTilesX` from texture dimensions for use by the hierarchical indirect dispatch compute shaders.

## Architecture Notes

Uniform population is split by subsystem to keep files small and focused. Wind and smoke each manage their own ping-pong index state via statics, toggling once per frame in their respective Uniforms files. The wind spread storage buffer (camera offset quad) is also written here, matching the pattern used by smoke.
