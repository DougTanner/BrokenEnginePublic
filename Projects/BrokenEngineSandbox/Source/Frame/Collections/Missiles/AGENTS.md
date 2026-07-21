# Missiles - Guided Area-Damage Projectiles

Guided missiles own client-side exhaust light, smoke trail, and looping sound effects while alive. Their damage is applied through an area-damage explosion rather than direct collision damage.

## Invariants

- Homing begins with a boost ramp, then tracks a subscribed target with smoothed steering. A newly acquired target affects homing on the next tick, preserving previous/current frame semantics.
- Validate and release target subscriptions on target loss, falling, explosion, transfer, and destruction. Removal is idempotent.
- Lifetime, velocity, falling state, boost-ramp delay, and turn rate cross cell boundaries; an arrival restores them verbatim and draws no fresh ramp delay. Falling disables propulsion, homing, target acquisition, and exhaust random draws; terrain impact at or below sea level removes the missile without explosion damage or effects.
- Resolve the earliest entity, terrain, or boundary event before applying explosion or transfer behavior. All missile damage enters through the area-damage registration at explosion time.
- Client-only pitch and exhaust values still draw from shared deterministic state. Both builds must execute those draws under the same shared-state conditions.
- Live transfer reuses smoke-trail identity; destination lights and sounds are recreated. Falling arrivals create no client effects.

## See Also

- [../AGENTS.md](../AGENTS.md) - Game collection transfer and collision rules
