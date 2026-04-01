# /Engine/Source/Frame/Collections/SmokeTrails/

Client-only externally-managed smoke trails with frame-rate-independent exponential position smoothing for trail geometry, using the Sync pattern.

## File Structure

The implementation is split across two `.cpp` files:
- **SmokeTrails.cpp** - Registration, lifecycle, update, sync, add/remove (with optional `reuseId` for trail continuity across parent respawn), collision phases
- **SmokeTrailsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- Smoothed positions are stored as SOA data (`pVecSmoothedPositions`) in `SmokeTrailsInterpolate`, copied during `AllocateAndCopy()` and carried across frames
- `Update()` applies `ExponentialInterpolant` smoothing each render frame; W == 0 signals an uninitialized smoothed position and causes an instant snap to the current position
- `Render()` builds oriented quads from the smoothed-to-current position delta, with jitter applied for visual variation. Trails with zero length are skipped
- `Render()` takes an extra `uiFrameId` parameter (excluded from `InterpolateRenderTypes` and called separately in the render pipeline)
- On full-state application during reconciliation, the client copies smoothed positions from the existing frame into the incoming frame (by count, not by ID) to preserve visual continuity

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
