# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/

AI-controlled enemy spaceships with health, weapons, behavior flags, freeze time, and per-instance skeletal animation. Compiles in both client and server builds. Each spaceship owns a pusher and target, plus client-only wind trail and animation time. Hit flash effects spawn controlled point lights at collision contact points (client-only). `Register()` also registers the enemy blaster type (with a camera-aligned point light) used when spaceships fire.

## AI Behavior

- **Behavior flags**: Flee (when near player, with hysteresis), return to island center (when far from origin, with hysteresis), exploding (health depleted)
- **Targeting hierarchy**: Return to origin > flee from player > chase nearest alive player
- **Rotation**: Exponential smoothing with different rates for fleeing vs pursuing
- **Health regen**: Gradual regen when alive and player is far enough away
- Targets are removed on death so homing missiles stop tracking

## File Structure

The implementation is split across three `.cpp` files:
- **Spaceships.cpp** - Registration, lifecycle (spawn/transfer/destroy), equality, server compare, client object hydration
- **SpaceshipsUpdate.cpp** - Update, AI behavior, target finding, terrain avoidance, add/remove, collision phases
- **SpaceshipsRender.cpp** - GPU model pipeline, skeletal animation, and draw submission (`#ifdef BT_CLIENT` only)

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
