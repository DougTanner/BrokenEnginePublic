# /Engine/Source/Frame/Collections/WindTrails/

Client-only directional wind simulation input quads rendered from previous-to-current position, using the Sync pattern. Uses the render-only state pattern with globally-keyed position history.

## File Structure

The implementation is split across three `.cpp` files:
- **WindTrails.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy)
- **WindTrailsUpdate.cpp** - Update, sync, add/remove, collision phases
- **WindTrailsRender.cpp** - GPU resources, render-only state management, draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- `BeginRender()` calls `EraseStaleRenderState()` (from `Collection.h`) to prune previous-position entries for trails no longer present in any active frame, keyed by `wind_trail_t`
- Each trail renders an oriented quad from base-height-projected previous position to current position, scaled by `fLengthMultiplier`; direction encodes wind velocity for the shader
- `Render()` takes an extra `uiFrameId` parameter (excluded from `InterpolateRenderTypes` and called separately in the render pipeline)
- `ResetRenderState()` clears cached positions for world reset (e.g., reconnect)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
