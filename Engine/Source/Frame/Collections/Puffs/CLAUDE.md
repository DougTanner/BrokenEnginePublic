# /Engine/Source/Frame/Collections/Puffs/

Client-only fire-and-forget smoke puffs with custom keyframe animation, using the Controller pattern.

## File Structure

The implementation is split across three `.cpp` files:
- **Puffs.cpp** - Registration, lifecycle, equality
- **PuffsUpdate.cpp** - Update, add/addControlled, collision phases
- **PuffsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
