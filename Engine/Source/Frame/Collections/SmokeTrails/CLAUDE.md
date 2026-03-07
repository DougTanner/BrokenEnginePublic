# /Engine/Source/Frame/Collections/SmokeTrails/

Client-only externally-managed smoke trails with frame-rate-independent exponential position smoothing for trail geometry, using the Sync pattern. Uses render-only state pattern with globally-keyed position history.

## File Structure

The implementation is split across three `.cpp` files:
- **SmokeTrails.cpp** - Registration, lifecycle, equality
- **SmokeTrailsUpdate.cpp** - Update, sync, add/remove, collision phases
- **SmokeTrailsRender.cpp** - GPU resources, render-only state management, draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
