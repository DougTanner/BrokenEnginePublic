# /Engine/Source/Frame/Collections/Billboards/

Client-only screen-space UI indicators with offscreen arrow handling, using the Sync pattern for parent-provided positional data.

## File Structure

The implementation is split across three `.cpp` files:
- **Billboards.cpp** - Registration, lifecycle, equality
- **BillboardsUpdate.cpp** - Update, sync, add/remove, collision phases
- **BillboardsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
