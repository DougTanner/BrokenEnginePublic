# Debug Shaders - Wireframe Debug Visualization

## Overview

Shaders for the `DebugRender` system, rendering wireframe primitives (boxes, spheres, circles, lines) in debug builds. Each per-instance storage buffer entry holds a row-major 3x4 affine transform (translation in each row's `.w`) plus a flat color; primitives draw via indirect line-list calls. The three shaders back four pipelines — boxes, spheres, and lines share the world-space vertex shader.

## Key Files

- **DebugRender.vert** - World-space vertex shader for boxes, spheres, and lines: applies per-instance transform from the storage buffer to unit mesh positions and passes color to the fragment stage
- **DebugRenderBillboard.vert** - Billboard vertex shader for circles: discards the instance transform's orientation, keeping only its center (the per-row `.w` translation) and uniform scale (first-column length), then rebuilds a camera-facing basis from `f4ToEyeNormal` so the circle stays flat-on to the view. The worldUp axis falls back from +Z to +X when the view is near-vertical — load-bearing, since the default top-down camera makes the naive +Z cross product degenerate
- **DebugRender.frag** - Fragment shader shared by all debug pipelines: outputs the flat (non-interpolated) per-instance color directly with no lighting

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shaders overview
- [../../Source/Graphics/Debug/CLAUDE.md](../../Source/Graphics/Debug/CLAUDE.md) - C++ DebugRender system that drives these shaders
