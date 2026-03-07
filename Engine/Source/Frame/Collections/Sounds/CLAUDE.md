# /Engine/Source/Frame/Collections/Sounds/

Client-only 3D spatial audio sources using the Sync pattern. Uses a separate `GenerateSoundUuid()` counter so sound ID generation does not affect deterministic UUID sequences.

## File Structure

The implementation is split across two `.cpp` files:
- **Sounds.cpp** - Registration, lifecycle, equality, GPU resources
- **SoundsUpdate.cpp** - Update, sync, add/remove, collision phases

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
