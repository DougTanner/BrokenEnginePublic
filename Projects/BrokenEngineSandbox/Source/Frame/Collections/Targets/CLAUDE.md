# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Targets/

Trackable world positions for missile guidance with stable ID-based references. Compiles in both client and server builds. Uses subscriber pattern for shared missile tracking with alignment-based filtering.

## File Structure

The implementation is split across two `.cpp` files:
- **Targets.cpp** - Type registration, allocate/copy, equality, lifecycle stubs (targets are created/removed by owners, not by frame phases)
- **TargetsUpdate.cpp** - Sync (owner writes position/type via ID lookup), add/remove with subscriber-counted lifetime

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
