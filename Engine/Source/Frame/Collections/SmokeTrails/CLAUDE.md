# /Engine/Source/Frame/Collections/SmokeTrails/

Client-only externally-managed smoke trails with frame-rate-independent exponential position smoothing for trail geometry, using the Sync pattern. Uses the render-only state pattern with globally-keyed smoothed position history.

## File Structure

The implementation is split across two `.cpp` files:
- **SmokeTrails.cpp** - Registration, lifecycle, update, sync, add/remove (with optional `reuseId` for trail continuity across parent respawn), collision phases
- **SmokeTrailsRender.cpp** - GPU resources, render-only state management, draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- `BeginRender()` calls `EraseStaleRenderState()` (from `Collection.h`) to prune smoothed-position entries for trails no longer present in any active frame, keyed by `smoke_trails_t`
- `Render()` applies `ExponentialInterpolant` smoothing to all trail positions (including culled ones) each render frame to maintain continuous history, then builds oriented quads from smoothed-to-current position with jitter
- `Render()` takes an extra `uiFrameId` parameter (excluded from `InterpolateRenderTypes` and called separately in the render pipeline)
- `ResetRenderState()` clears cached positions for world reset (e.g., reconnect)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
