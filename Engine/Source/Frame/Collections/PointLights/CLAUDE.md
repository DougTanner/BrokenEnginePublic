# /Engine/Source/Frame/Collections/PointLights/

Client-only circular point lights with optional keyframe animation, using the Sync and Controller patterns.

## File Structure

The implementation is split across three `.cpp` files:
- **PointLights.cpp** - Registration, lifecycle, equality
- **PointLightsUpdate.cpp** - Update, sync, add/remove/addControlled, collision phases
- **PointLightsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
