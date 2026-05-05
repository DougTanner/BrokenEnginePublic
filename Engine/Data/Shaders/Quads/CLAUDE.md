# Quads Shaders - Quad Vertex Shaders

## Overview

Vertex shaders for instanced quad rendering. Each shader reads per-instance data from a storage buffer (indexed by `gl_InstanceIndex`) and outputs position, texcoords, and per-instance parameters to fragment shaders.

## Shaders

- **QuadsVisibleArea.vert** - Renders quads using `QuadLayout` (non-axis-aligned, per-vertex positions/texcoords). Projects world positions into clip space relative to a visible area selected by push constant (camera, shadow, smoke, or lighting area). Outputs world position and world center (average of all 4 vertex positions) for use by deposit fragment shaders (e.g., `AreaLight.frag`) to compute EWNS directional weights
- **QuadsAxisAlignedVisibleArea.vert** - Renders quads using `AxisAlignedQuadLayout` (rect + texcoord rect + rotation). Projects into the same push-constant-selected visible areas as `QuadsVisibleArea.vert`. Outputs world position and world center varyings (computed from `f4VertexRect`) for use by deposit fragment shaders. **Rotates each corner around the quad center by `fRotation` (radians)** — zero is identity, so unset/non-island consumers render axis-aligned with no caller action required. The vertex shader computes `(cos, sin)` once and forwards it via `f2OutRotationCosSin` (location 6) for fragment shaders that must rotate sampled vectors (e.g., `TerrainNormal.frag` rotating BC5 normal tangents). `Islands::UpdateActiveIslands` populates `fRotation` from `FrameStaticData::fIslandRotation`.
- **QuadsAxisAligned.vert** - Renders axis-aligned quads in NDC (no projection), outputting color packed as a uint varying
- **QuadsFullscreen.vert** - Renders a single fullscreen triangle/quad (no instance data)

## Architecture Notes

Visible-area selection is driven by a push constant integer: 0 = camera visible area, 1 = shadow extra area, 2 = smoke area, 3 = lighting area. This avoids separate pipeline permutations for each render target coordinate space.
