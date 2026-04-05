# Debug Shaders - Wireframe Debug Visualization

## Overview

Shaders for the `DebugRender` system, rendering world-space wireframe primitives (boxes, spheres, circles, lines) in `BT_DEBUG` builds. Uses per-instance storage buffers with a compact 3x4 transform and color, drawn via indirect calls with `VK_PRIMITIVE_TOPOLOGY_LINE_LIST`.

## Key Files

- **DebugRender.vert** - Vertex shader: applies per-instance transform from the storage buffer to unit mesh positions and passes color to the fragment stage
- **DebugRender.frag** - Fragment shader: outputs the interpolated per-instance color directly with no lighting

## See Also

- [../CLAUDE.md](../CLAUDE.md) - Parent shaders overview
- [../../Source/Graphics/Debug/CLAUDE.md](../../Source/Graphics/Debug/CLAUDE.md) - C++ DebugRender system that drives these shaders
