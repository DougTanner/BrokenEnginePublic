# /Engine/Source/Frame/Collections/WindTrails/

Client-only directional wind simulation input quads rendered from previous-to-current position, using the Sync pattern. Uses render-only state pattern with globally-keyed position history.

## File Structure

The implementation is split across three `.cpp` files:
- **WindTrails.cpp** - Registration, lifecycle, equality
- **WindTrailsUpdate.cpp** - Update, sync, add/remove, collision phases
- **WindTrailsRender.cpp** - GPU resources, render-only state management, draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
