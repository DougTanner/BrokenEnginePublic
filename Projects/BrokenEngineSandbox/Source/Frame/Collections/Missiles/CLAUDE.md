# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/

Guided missiles with homing AI and visual effects. Compiles in both client and server builds. Self-contained GPU model pipeline. Each missile owns a pusher, plus client-only area light, smoke trail, and sound.

## Architecture Notes

- **Homing AI**: Two-phase guidance -- initial boost ramps rotation strength from zero to full over a short delay, then active tracking continuously orients toward the target with exponential smoothing. Falls back to stored direction if target is lost
- **Area damage**: Missiles deal zero direct collision damage; all damage comes through area-of-effect explosions with per-missile configurable radius
- **Owned objects**: Each missile syncs a pusher (shared), plus client-only area light, smoke trail, and sound

## File Structure

The implementation is split across three `.cpp` files:
- **Missiles.cpp** - Registration, lifecycle (spawn/transfer/destroy), equality, server compare, client object hydration
- **MissilesUpdate.cpp** - Update, sync, homing AI, add/remove, collision phases, area damage
- **MissilesRender.cpp** - GPU model pipeline, resources, and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
