# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/

Fast-moving energy projectiles with shared BlasterType configuration. Compiles in both client and server builds. Terrain impacts spawn visual and audio effects. Each blaster owns an area light, wind trail, and sound (client-only via Sync pattern).

## File Structure

The implementation is split across three `.cpp` files:
- **Blasters.cpp** - Registration, lifecycle (spawn/transfer/destroy), equality, server compare, client object hydration
- **BlastersUpdate.cpp** - Update, sync, add/remove, collision phases
- **BlastersRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
