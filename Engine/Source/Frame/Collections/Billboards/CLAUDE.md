# /Engine/Source/Frame/Collections/Billboards/

Client-only screen-space UI indicators with offscreen-arrow handling. Owner-driven: parents push position, type, flags, rotation, and an extra slot via `Sync()` each frame.

## Unique Aspects

- Offscreen flags select whether an indicator renders only when its world position projects outside the viewport, and whether it rotates to point at the offscreen target. Offscreen-only indicators are clamped to the viewport edge (aspect-corrected) rather than hidden, producing edge arrows. The per-element extra slot carries the NDC margin for the offscreen visibility test
- All projection, offscreen culling/clamping, and offscreen-rotate orientation run in the render phase because they depend on the live view/projection matrices; update/post-render phase bodies are empty
- `Add` rvalue-id overload is deleted to force callers to bind a persistent lvalue handle

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Sync/render-state patterns, owner-driven lifetime and transfer
