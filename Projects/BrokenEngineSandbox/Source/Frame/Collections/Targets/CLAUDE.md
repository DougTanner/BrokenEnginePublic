# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/

Trackable world positions for missile guidance with stable ID-based references. Compiles in both client and server builds. Uses subscriber pattern for shared missile tracking with alignment-based filtering.

## File Structure

The implementation is split across two `.cpp` files:
- **Targets.cpp** - Registration, lifecycle (spawn/transfer/destroy), equality, GPU resources
- **TargetsUpdate.cpp** - Update, sync, add/remove (subscriber pattern), collision phases

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
