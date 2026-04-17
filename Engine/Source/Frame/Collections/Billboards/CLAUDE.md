# /Engine/Source/Frame/Collections/Billboards/

Client-only screen-space UI indicators with offscreen-arrow handling.

## Unique Aspects

- Offscreen behavior: flags select whether the indicator renders only when its world position falls outside the viewport, and whether it rotates to point at the offscreen target. NDC margin for the offscreen visibility test is carried in the per-element extra slot
- Orientation math for offscreen-rotated billboards runs in the render phase (not update) because it depends on the live view/projection matrices
- Positional state is pushed by owners each frame; the update/post-render phase bodies are intentionally empty
- Transfer across coord boundaries is a no-op: owned billboards follow their parent entity, which re-adds on the destination frame
- `Add` rvalue-id overload is deleted to force callers to bind a persistent lvalue handle

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Sync/Controller/render-state patterns
