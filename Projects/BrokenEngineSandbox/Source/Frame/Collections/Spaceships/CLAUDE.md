# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Spaceships/

AI-controlled enemy spaceships with health, weapons, behavior flags, freeze time, and per-instance skeletal animation. Compiles in both client and server builds. Each spaceship owns a pusher and target, plus client-only wind trail and animation time. Hit flash effects spawn controlled point lights at collision contact points (client-only). `Register()` also registers the enemy blaster type (with a camera-aligned point light) used when spaceships fire.

## Sizing

`kfSpaceshipRadius` (defined in `Spaceships.h`) is the single source of truth for spaceship body size. All body-size-dependent values — pusher radius, explosion sizes/jitter/trail/lighting, blaster size, target size, hit flash areas, terrain collision displacement, and model visual scale — derive from it via ratio multipliers. Adjust `kfSpaceshipRadius` to proportionally rescale all spaceship-related sizes at once.

## AI Behavior

- **Behavior flags**: Flee (when near player, with hysteresis), return to island center (when far from origin, with hysteresis), exploding (health depleted)
- **Targeting hierarchy**: Return to origin > flee from player > chase nearest alive player
- **Rotation**: Exponential smoothing with different rates for fleeing vs pursuing
- **Health regen**: Gradual regen when alive and player is far enough away
- Targets are removed on death so homing missiles stop tracking
- Pusher overlap response uses `engine::ApplyClampedPush()` to apply a clamped impulse (velocity capped at `kfSpaceshipMaxPusherPushVelocity`, derived from `kfSpaceshipMaxSpeed * 0.5f`)
- Arriving spaceships receive an arrival grace period that makes them invisible to player targeting scans for a short window; the timer is carried in `TransferData`. See [Arrival Grace Period](../CLAUDE.md) in the Collections CLAUDE.md.

## File Structure

The implementation is split by subsystem across four `.cpp` files sharing `Spaceships.h`:
- **Spaceships.cpp** - Registration, lifecycle (Spawn/Transfer/Destroy), client object hydration, `SpaceshipsInterpolate::Update`, and the `SpaceshipsPostRender::Update` orchestrator that loads per-entity state and calls into Navigation/Combat helpers
- **SpaceshipsNavigation.cpp** - Per-iter nav helpers (`ComputeSteering`, `ApplyMovement`, `ApplyTerrainBounce`), the separate `AvoidTerrain` pass, and navigation constants (acceleration, rotation smoothing, flee/return hysteresis, terrain bounce params)
- **SpaceshipsCombat.cpp** - Collision phases (`PreCollision`/`PostCollision`/`AreaDamage`), `BeginExplosion`, per-iter helpers (`RegenerateHealth`, `ApplyDeathKnockback`), collision layer `thread_local` arrays, and combat constants (death sounds, health regen, knockback speed)
- **SpaceshipsRender.cpp** - GPU model pipeline, skeletal animation, and draw submission (`#ifdef BT_CLIENT` only)

The per-iter helpers are declared `private:` on `SpaceshipsPostRender` in `Spaceships.h` and called from the single orchestrator loop — matching the Players collection's pattern.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
