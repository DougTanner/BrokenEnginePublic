# /Engine/Source/Frame/Collections/HexShields/

Client-only geodesic shield meshes with directional damage visualization, using the Sync pattern.

## File Structure

The implementation is split across three `.cpp` files:
- **HexShields.cpp** - Registration, lifecycle, equality
- **HexShieldsUpdate.cpp** - Update, sync, add/remove, collision phases
- **HexShieldsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
