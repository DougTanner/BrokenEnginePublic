# /Engine/Source/Frame/Collections/Billboards/

Client-only screen-space UI indicators with offscreen-arrow handling. Owner-driven: parents push position, type, flags, rotation, and an extra slot via `Sync()` each frame. Fully wired into the frame walk and render pass, but no game-side consumer exists yet — no types registered, no callers of Add/Sync/Remove.

## Unique Aspects

- Offscreen flags select whether an indicator renders only when its world position projects outside the viewport, and whether it rotates to point at the offscreen target. Offscreen-only indicators are clamped to the viewport edge (aspect-corrected) rather than hidden, producing edge arrows. The per-element extra slot carries the NDC margin for the offscreen visibility test
- All projection, offscreen culling/clamping, and offscreen-rotate orientation run in the render phase because they depend on the live view/projection matrices
- Type size is the quad's NDC half-height; the render-phase edge clamp mirrors the vertex shader's sizing math (x extent divided by aspect ratio), so the two must change in lockstep
- `Add` rvalue-id overload is deleted to force callers to bind a persistent lvalue handle
