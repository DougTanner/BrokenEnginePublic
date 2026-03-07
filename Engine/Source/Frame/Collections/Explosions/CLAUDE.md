# /Engine/Source/Frame/Collections/Explosions/

Composite explosion effects that spawn lights, puffs, smoke trails, wind radials, and GPU particles. Compiles in both client and server builds, with visual spawning client-only. Game code registers custom explosion types.

## File Structure

The implementation is split across two `.cpp` files:
- **Explosions.cpp** - Registration, type registry, lifecycle (spawn/transfer/destroy), equality, GPU resources
- **ExplosionsUpdate.cpp** - Update, sync, add/remove, collision phases

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
