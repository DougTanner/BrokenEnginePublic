# /Engine/Source/Frame/Collections/Billboards/

Client-only screen-space UI indicators with offscreen arrow handling, using the Sync pattern for parent-provided positional data.

## File Structure

The implementation is split across three `.cpp` files:
- **Billboards.cpp** - Registration, lifecycle (allocate/copy, spawn, transfer, destroy)
- **BillboardsUpdate.cpp** - Update, sync, add/remove, collision phases
- **BillboardsRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## Architecture Notes

- `BillboardFlags` control offscreen behavior: `kOffscreenOnly` renders only when the world position is outside the viewport, `kOffscreenRotate` auto-rotates the billboard to point toward the offscreen target
- `Render()` projects world positions to clip space, clamps offscreen indicators to screen edges, and resolves texture indices via `gpTextureManager->mTextureDescriptors.CrcToIndex(rType.crc)`

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
