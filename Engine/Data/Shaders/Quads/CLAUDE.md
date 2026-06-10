# Quads Shaders - Instanced Quad Vertex Shaders

## Overview

Vertex-stage family for instanced quad rendering, mixed and matched with fragment shaders per pipeline. All four read corner positions from the shared 4-vertex unit-square buffer (`BufferManager::mQuadsVertexBuffer`, location 0); per-instance data comes from a `readonly` SSBO indexed by `gl_InstanceIndex`.

## Shaders

- **QuadsFullscreen.vert** - Maps the unit square straight to NDC; no instance data. Used by fullscreen/RTT passes (debug texture, lighting spread, smoke clear).
- **QuadsAxisAligned.vert** - Axis-aligned quads emitted directly in NDC (no projection), with a packed-uint color varying; UI-space quads (profiler text).
- **QuadsVisibleArea.vert** - Fully general quads via `QuadLayout`: per-vertex world positions/texcoords (corner-to-array index derived as `2*v + u`), so quads need not be axis-aligned or rectangular. Projects world XY against the push-constant-selected visible area; outputs world position and world center (average of the 4 vertices) for deposit fragment shaders.
- **QuadsAxisAlignedVisibleArea.vert** - Compact world-space rect form via `AxisAlignedQuadLayout`. Rotates each corner around the quad center by `fRotation` (radians; 0 = identity, so non-rotating consumers need no caller action), then projects through the same visible-area selection. Forwards the per-instance bindless texture slot (flat) so terrain G-buffer frags route their per-island sampler index per quad, decoupled from `gl_InstanceIndex`; island placement is the only producer of nonzero rotation/slot values. Also forwards `(cos, sin)` as a flat varying — currently unconsumed by any paired frag (`Terrain.vert` duplicates the same computation at its own location for `Terrain.frag`'s BC5 normal rotation).

## Architecture Notes

- **Visible-area selection**: push-constant integer picks the projection space — 0 camera, 1 shadow extra, 2 smoke, 3 lighting. One vert serves four render-target coordinate spaces without pipeline permutations, which matters because command buffers are recorded once and resubmitted.
- **Texcoord lerp** `(1-t)*min + t*max` across a texture rect allows flipped/sub-rect sampling per instance.
- **Degenerate-quad culling**: zero-size rects collapse to degenerate triangles — free GPU-side culling that lets fixed-size instance SSBOs (e.g., island placements) stay fully drawn under record-once command buffers.
- `f4Params`/`pf4Params` are opaque pass-throughs; semantics belong to the paired frag.

## Shared-vert interface rule

These verts pair with many frags (terrain G-buffer, deposits, lighting, smoke, wind), so vert/frag interfaces must stay matched: a frag input with no matching vert output is a Vulkan error (`VUID-RuntimeSpirv-OpEntryPoint-08743`); an unconsumed vert output is only informational (`WARNING-Shader-OutputNotConsumed`). Any location a shared frag reads must exist on both `*VisibleArea.vert` files — e.g., the texture slot (location 7) is mirrored, with `QuadsVisibleArea.vert` writing a dummy `0`. The rotation cos/sin (location 6) is axis-aligned-only, so frags that can pair with both verts must not read it. Location 3 is intentionally skipped in both interfaces; respect the mirroring rule before reusing it. Frag-side dummy inputs are named `*Unused` to flag intent.
