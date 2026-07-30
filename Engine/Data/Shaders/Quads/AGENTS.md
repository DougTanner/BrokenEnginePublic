# Quad Shaders - Reusable Instanced Vertex Paths

This family supplies fullscreen, world-visible-area, and compact axis-aligned quad vertices for many fragment pipelines.

## Invariants

- A push constant selects camera, shadow, smoke, or lighting projection for visible-area variants. This allows record-once command buffers to reuse the same pipelines across target spaces.
- The windowed lighting variant maps its quad through the published per-pass UV rectangle. Keep that mapping paired with the bounded spread attachments rather than expanding the draw to the full target.
- Zero-size rectangles intentionally collapse to degenerate triangles, allowing fixed-capacity instance buffers to cull entries without changing recorded draw counts.
- General quads carry explicit corner positions; compact axis-aligned quads derive corners from center, size, and rotation. Texture slots are instance data and are independent of `gl_InstanceIndex`.
- Fragment inputs shared by the general and axis-aligned visible-area variants require compatible outputs from both vertex shaders. Keep their stage interfaces mirrored semantically; do not rely on one consumer's unused locations.
- Opaque pass-through parameters belong to the paired fragment shader, not this shared vertex family.
