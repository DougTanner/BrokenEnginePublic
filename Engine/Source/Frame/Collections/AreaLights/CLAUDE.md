# /Engine/Source/Frame/Collections/AreaLights/

Client-only quad-based area lights for projectiles and effects, using the Sync pattern for parent-provided positional data.

## File Structure

The implementation is split across three `.cpp` files:
- **AreaLights.cpp** - Registration, lifecycle, equality
- **AreaLightsUpdate.cpp** - Update, sync, add/remove, collision phases
- **AreaLightsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
