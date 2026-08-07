# Quad Shaders - Reusable Instanced Vertex Paths

This family supplies fullscreen, world-visible-area, and compact axis-aligned quad vertices for many fragment pipelines.

## Invariants

- A push constant selects camera, shadow, smoke, or lighting projection for visible-area variants. This allows record-once command buffers to reuse the same pipelines across target spaces.
- Zero-size rectangles intentionally collapse to degenerate triangles, allowing fixed-capacity instance buffers to cull entries without changing recorded draw counts.
- General quads carry explicit corner positions; compact axis-aligned quads derive corners from center, size, and rotation. Texture slots are instance data and are independent of `gl_InstanceIndex`.
- Fragment inputs shared by the general and axis-aligned visible-area variants require compatible outputs from both vertex shaders, and the failure is one-directional: a fragment input with no matching vertex output fails pipeline creation (`VUID-RuntimeSpirv-OpEntryPoint-08743`), while a vertex output nobody reads is only an informational message (`WARNING-Shader-OutputNotConsumed`). So every location a shared fragment shader declares must exist on both `*VisibleArea.vert` files — the texture slot at location 7 is mirrored that way, with `QuadsVisibleArea.vert` writing a dummy `0`. Deleting the seemingly unused fragment input is the wrong fix.
- Locations 3 and 6 are skipped in both visible-area vertex interfaces. Reusing one means adding it to both, under the same mirroring rule.
- Opaque pass-through parameters belong to the paired fragment shader, not this shared vertex family.
