# Spaceships - Enemy Combat Ships

Enemy spaceships combine terrain-aware steering, health and damage, weapon fire, and client skeletal rendering.

## Behavior Invariants

- Steering prioritizes returning to an island, fleeing a nearby player, then chasing the nearest alive player. Return and flee transitions use distance hysteresis.
- Steering turn rate crosses cell boundaries with the ship rather than restarting on arrival.
- Direct blaster hits arrive through collision; missile splash damage arrives through the area-damage phase. Destruction applies knockback and a staged explosion sequence.
- Body, collision, effect, and model sizing derive from the shared spaceship radius.

## Rendering Invariants

- Rendering across active coordinates is sequential on the main thread because the visible accumulator is process-wide rather than thread-local. Do not parallelize the outer per-coordinate calls.
- Within one coordinate, culling and slab reservation happen first; workers then write disjoint mesh-data and joint-matrix slabs. Preserve those non-overlapping reservations when changing dispatch.
