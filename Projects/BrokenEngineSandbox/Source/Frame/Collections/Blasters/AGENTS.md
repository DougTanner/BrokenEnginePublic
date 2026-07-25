# Blasters - Fast Energy Projectiles

Blasters move at constant velocity and own client-side light and wind-trail effects. They render no projectile geometry directly.

## Invariants

- Resolve exactly one earliest event over swept entity collision, terrain impact, and frame-boundary transfer. Terrain and boundary events win exact-time ties through the collision layer cutoff.
- Shared pitch and impact-jitter draws remain outside client guards even when only client effects consume them. Moving these draws changes the deterministic random stream.
- Each blaster uses either an area light or a camera-aligned point light. Sync and teardown must preserve that exclusive ownership.
- Cross-cell transfer carries wind-trail tuning, not trail identity. The source trail is removed and the destination client creates a new trail.
- Terrain impacts use the resolved elevation-grid position before spawning their client effects.
- Weapon code creates new projectiles. Keep one-shot muzzle audio at the firing site so transfers do not replay it.
