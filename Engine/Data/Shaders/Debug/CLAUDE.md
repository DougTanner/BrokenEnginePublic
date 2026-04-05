# Debug Shaders - Wireframe Debug Visualization

## Overview

Shaders for the `DebugRender` system, rendering wireframe primitives (boxes, spheres, circles, lines) in `BT_DEBUG` builds. Uses per-instance storage buffers with a compact 3x4 transform and color, drawn via indirect calls with `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`.

## Key Files

- **DebugRender.vert** - World-space vertex shader for boxes, spheres, and lines: applies per-instance transform from the storage buffer to unit mesh positions and passes color to the fragment stage
- **DebugRenderBillboard.vert** - Billboard vertex shader for circles: expands an XY-plane unit circle mesh to always face the camera, keeping circles camera-facing regardless of view angle
- **DebugRender.frag** - Fragment shader shared by all debug primitives: outputs the interpolated per-instance color directly with no lighting

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shaders overview
- [../../Source/Graphics/Debug/CLAUDE.md](../../Source/Graphics/Debug/CLAUDE.md) - C++ DebugRender system that drives these shaders
