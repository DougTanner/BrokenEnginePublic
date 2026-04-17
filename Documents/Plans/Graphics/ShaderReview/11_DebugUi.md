# Shader Review — Debug + UI

Files: `DebugRender.vert`, `DebugRenderBillboard.vert`, `DebugRender.frag`, `UiDepthPrepass.vert`, `UiDepthPrepass.frag`, `ProfileText.frag`

## PASS

- `Engine/Data/Shaders/Debug/DebugRender.vert` — correct sets, scalar SSBO, `readonly`, uses `Transform`.
- `Engine/Data/Shaders/Debug/DebugRenderBillboard.vert` — billboard branch is uniform; `abs(forward.z) < 0.999` guard prevents degenerate cross.
- `Engine/Data/Shaders/Debug/DebugRender.frag` — trivial flat color passthrough.
- `Engine/Data/Shaders/Ui/UiDepthPrepass.vert` — tiny, clean; single note below.
- `Engine/Data/Shaders/Ui/UiDepthPrepass.frag` — empty `main()` for depth-only prepass.
- `Engine/Data/Shaders/Ui/ProfileText.frag` — trivially correct texture alpha multiply.

## Minor notes

### `Engine/Data/Shaders/Debug/DebugRenderBillboard.vert`

- line 35 — `normalize(mainLayout.f4ToEyeNormal.xyz)` is redundant if the field is already unit by convention (sibling code treats `f4SunMoonNormal.xyz` as pre-normalized). Vertex-stage cost is negligible; drop for clarity.

### `Engine/Data/Shaders/Ui/UiDepthPrepass.vert`

- line 17 — depth prepass writes `gl_Position.z = 0.0`. If a subsequent UI color pass uses `VK_COMPARE_OP_EQUAL`, both pipelines must produce bit-identical `gl_Position`. Consider `invariant gl_Position;` here and in the paired color vert, sharing the identical expression.

### `Engine/Data/Shaders/Ui/ProfileText.frag`

- lines 10-11 — `iInInstanceIndex` and `f4InMisc` declared but never used. Dead varyings waste interpolator slots; remove here (and from the vert) unless a pending feature needs them.
