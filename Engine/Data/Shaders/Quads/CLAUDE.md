# Quads Shaders - Quad Vertex Shaders

## Overview

Vertex shaders for instanced quad rendering. Each shader reads per-instance data from a storage buffer (indexed by `gl_InstanceIndex`) and outputs position, texcoords, and per-instance parameters to fragment shaders.

## Shaders

- **QuadsVisibleArea.vert** - Renders quads using `QuadLayout` (non-axis-aligned, per-vertex positions/texcoords). Projects world positions into clip space relative to a visible area selected by push constant (camera, shadow, smoke, or lighting area). Outputs world position and world center (average of all 4 vertex positions) for use by deposit fragment shaders (e.g., `AreaLight.frag`) to compute EWNS directional weights
- **QuadsAxisAlignedVisibleArea.vert** - Renders quads using `AxisAlignedQuadLayout` (rect + texcoord rect + rotation + bindless texture slot). Projects into the same push-constant-selected visible areas as `QuadsVisibleArea.vert`. Outputs world position and world center varyings (computed from `f4VertexRect`) for deposit fragment shaders. **Rotates each corner around the quad center by `fRotation` (radians)** — zero is identity, so unset/non-island consumers render axis-aligned with no caller action required. Computes `(cos, sin)` once and forwards it as a flat varying for fragment shaders that must rotate sampled vectors (`Terrain.frag` rotates its BC5 normal tangents this way, plumbed by `Terrain.vert`). The per-instance texture slot is forwarded as a flat varying so terrain G-buffer fragment shaders route their bindless sampler index per quad (decoupled from `gl_InstanceIndex`). `Islands::UpdateActiveIslands` populates rotation and texture slot from each cell's `FrameStaticData::islands` placement entry.
- **QuadsAxisAligned.vert** - Renders axis-aligned quads in NDC (no projection), outputting color packed as a uint varying
- **QuadsFullscreen.vert** - Renders a single fullscreen triangle/quad (no instance data)

## Architecture Notes

Visible-area selection is driven by a push constant integer: 0 = camera visible area, 1 = shadow extra area, 2 = smoke area, 3 = lighting area. This avoids separate pipeline permutations for each render target coordinate space.

## Shared-vert interface rule

These verts are reused across many pipelines (terrain G-buffer, deposits, lighting, smoke, wind). Any new `out` location added to `QuadsVisibleArea.vert` or `QuadsAxisAlignedVisibleArea.vert` MUST be either (a) declared as a matching `in` on every paired frag, or (b) added to BOTH verts when paired frags can pair with either — otherwise Vulkan validation fires `WARNING-Shader-OutputNotConsumed` (informational) on one side or `VUID-RuntimeSpirv-OpEntryPoint-08743` (error) on the other. The two `*VisibleArea.vert` outputs overlap deliberately: any location a shared frag reads from either vert exists on both — e.g. the texture slot (location 7) is mirrored, with `QuadsVisibleArea.vert` writing a dummy `0`. The rotation cos/sin (location 6) is axis-aligned-only, so frags that pair with both verts must not read it. Frag-side dummy inputs are named `*Unused` to flag intent.
