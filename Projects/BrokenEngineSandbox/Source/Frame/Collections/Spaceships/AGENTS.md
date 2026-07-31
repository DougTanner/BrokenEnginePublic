# Spaceships - Enemy Combat Ships

Enemy spaceships combine terrain-aware steering, health and damage, weapon fire, and client skeletal rendering.

## Behavior Invariants

- Steering prioritizes returning to an island, fleeing a nearby player, then chasing the nearest alive player. Return and flee transitions use distance hysteresis.
- Steering turn rate crosses cell boundaries with the ship rather than restarting on arrival.
- Direct blaster hits arrive through collision; missile splash damage arrives through the area-damage phase. Destruction applies knockback and a staged explosion sequence.
- Body, collision, effect, and model sizing derive from the shared spaceship radius.
- The PostRender Spawn phase hook never creates spaceships, despite the name: it emits the staggered death explosions and fires enemy blasters. Creation is the separate `SpawnInfo` overload of `Spawn`, called from the frame's group spawner and from cross-cell transfer. Enemy-creation logic belongs in that overload, not in the phase hook, which runs per tick per ship inside the blaster-fire loop.
- Cross-phase field ownership: the steering delta rotation (this tick's turn amount) lives in the Interpolate struct and is read by Interpolate Update and by Render, but it is decayed and written from PostRender Update. That one crossing is deliberate — do not "fix" it by moving the field to PostRender, which would change the CRC member set and the render read path, and do not treat other Interpolate fields as writable from PostRender.

## Rendering Invariants

- Rendering across active coordinates is sequential on the main thread because the running total used for what is displayed is process-wide rather than thread-local. Do not parallelize the outer per-coordinate calls.
- Within one coordinate, culling happens first, followed by one up-front bulk reservation each for the mesh-data and joint-matrix blocks; workers then write disjoint mesh-data and joint-matrix slabs. Preserve those non-overlapping reservations when changing dispatch.
