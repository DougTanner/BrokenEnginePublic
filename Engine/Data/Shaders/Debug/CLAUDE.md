# Debug Shaders - Wireframe Debug Visualization

## Overview

Shaders for the `DebugRender` system, rendering wireframe primitives (boxes, spheres, circles, lines) in debug builds. Each per-instance storage buffer entry holds a row-major 3x4 affine transform (translation in each row's `.w`) plus a flat color; primitives draw via indirect line-list calls.

## Key Files

- **DebugRender.vert** - World-space vertex shader for boxes, spheres, and lines: applies per-instance transform from the storage buffer to unit mesh positions and passes color to the fragment stage
- **DebugRenderBillboard.vert** - Billboard vertex shader for circles: discards the instance transform's orientation, extracting only its center (the `.w` column) and uniform scale, then rebuilds a camera-facing basis from `f4ToEyeNormal` so the circle stays flat-on to the view regardless of angle
- **DebugRender.frag** - Fragment shader shared by all debug primitives: outputs the interpolated per-instance color directly with no lighting

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shaders overview
- [../../Source/Graphics/Debug/CLAUDE.md](../../Source/Graphics/Debug/CLAUDE.md) - C++ DebugRender system that drives these shaders
