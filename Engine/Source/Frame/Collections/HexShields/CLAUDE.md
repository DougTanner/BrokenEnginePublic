# /Engine/Source/Frame/Collections/HexShields/

Client-only geodesic shield meshes with directional damage visualization, using the Sync pattern.

## File Structure

The implementation is split across three `.cpp` files:
- **HexShields.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy)
- **HexShieldsUpdate.cpp** - Update, sync, add/remove, collision phases
- **HexShieldsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- `SyncData` carries decomposed transform matrices and per-direction damage intensities for multi-directional hit visualization
- Supports color mixing between base and lighting colors, plus configurable shield glow intensity

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
