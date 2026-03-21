# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/

Fast-moving energy projectiles with shared `BlasterType` configuration. Compiles in both client and server builds. Terrain impacts spawn visual and audio effects.

## Light Type Selection

Each blaster type uses either an area light or a camera-aligned point light for its visual, selected by `BlastersType::uiPointLightTypeIndex` (0xFF = use area light; any other value = point light). Player blasters use area lights; enemy blasters use camera-aligned point lights registered in `Spaceships.cpp`. Client-owned light handles (`puiAreaLights`, `puiPointLights`) and wind trail/sound handles are stored in `BlastersInterpolate` (SOA, `#ifdef BT_CLIENT`).

## File Structure

The implementation is split across three `.cpp` files:
- **Blasters.cpp** - Registration, lifecycle (spawn/transfer/destroy), equality, server compare, client object hydration
- **BlastersUpdate.cpp** - Update, sync, add/remove, collision phases
- **BlastersRender.cpp** - GPU resources and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
