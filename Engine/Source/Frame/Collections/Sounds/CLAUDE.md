# /Engine/Source/Frame/Collections/Sounds/

Client-only 3D spatial audio sources using the Sync pattern. Uses a separate `GenerateSoundUuid()` counter so sound ID generation does not affect deterministic UUID sequences.

## File Structure

The implementation is split across two `.cpp` files:
- **Sounds.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy), render placeholder (empty `Render()`)
- **SoundsUpdate.cpp** - Update, sync, add/remove, collision phases

## Architecture Notes

- Sound IDs use a separate UUID counter to avoid perturbing deterministic sequences shared with the server
- No GPU resources or rendering; audio playback is handled by AudioManager, not the render pipeline

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
