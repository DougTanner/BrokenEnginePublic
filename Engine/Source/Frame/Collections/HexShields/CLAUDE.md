# /Engine/Source/Frame/Collections/HexShields/

Client-only geodesic shield meshes with directional damage visualization, using the Sync pattern.

## File Structure

The implementation is split across three `.cpp` files:
- **HexShields.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy)
- **HexShieldsUpdate.cpp** - Update, sync, add/remove, collision phases
- **HexShieldsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- `SyncData` carries position, size, color mix, glow intensity, decomposed 3-row transform and normal matrices, and 16-direction damage channels with separate vertex-stage and fragment-stage intensities
- Directional damage enables multi-directional hit visualization with up to 16 simultaneous impact directions
- Supports color mixing between base and lighting colors, plus configurable shield glow intensity

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
