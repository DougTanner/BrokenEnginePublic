# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/

Fast-moving energy projectiles with shared `BlastersType` configuration. Blasters render no geometry of their own — `Render()` only sets profile counters; all visuals are owned engine objects (area/point light + wind trail) created at `ClientInit` and synced each interpolate tick.

## Game-Specific Behavior

- **Motion & collision**: Constant-velocity linear integration with swept-sphere tests over each blaster's exact active interval. A blaster resolves exactly one earliest event: entity collision, point-swept terrain impact, or frame-boundary transfer; terrain and boundary events win exact-time ties through the collision layer's exclusive cutoff. Direction is re-derived (renormalized from velocity) each interpolate tick, not integrated — so positions/directions are recomputed rather than memcpy'd forward in `AllocateAndCopy`.
- **Light type selection**: Each blaster type uses either an area light or a camera-aligned point light, chosen by a per-type index (`0xFF` sentinel = area light). Invariant: exactly one of the per-entity area-light / point-light refs is valid; sync and teardown branch on which is present. `BlastersType` entries are registered by the firing collections (`Players.cpp` player/area light, `Spaceships.cpp` enemy/point light) — Blasters' own `Register()` registers only terrain-impact effect types (idempotent, safe to call repeatedly).
- **Impact effects**: Terrain impacts use the deterministic elevation-grid trace position, destroy the blaster, and emit a crater point light, smoke puff, and one-shot sound (client-only). Effect keyframes scale through game-side `LightingWrappers` / `SmokeWrappers`.
- **Shared random-engine discipline**: `pfPitches` is seeded at Spawn from `rFrame.postRender.randomEngine` and is a shared (not client-only) field; the impact-jitter and rotation draws in `PostCollision` sit outside the `BT_CLIENT` guard. Both exist so client and server advance the random engine identically even though only the client consumes the results — do not move these draws inside client guards. The pitch feeds the per-blaster looping sound, currently disabled (`SoundsInterpolate::Sync` calls commented out as placeholder audio); the sound object is still allocated/freed for lifetime parity.
- **Cross-cell transfer**: `TransferRequest::data` carries the wind-trail params under `#ifdef BT_CLIENT` so visuals survive a cell handoff.

## Spawning

Two overloads exist. The phase-dispatched `Spawn(Frame, FrameStaticData)` is intentionally empty — blasters have no server-driven spawner. New blasters come only from the `Spawn(Frame, SpawnInfo&)` overload invoked by weapon code. Do not add server-driven spawn logic to the phase entry point.

Player-fired blasters are created during the Spawn phase after collision and first participate in collision on the next tick. Existing blasters sweep their normal previous-to-current movement interval with terrain and frame-boundary cutoffs.

The fire-and-forget muzzle one-shot is emitted at the weapon firing site (Players/Spaceships combat code), not inside `Spawn(SpawnInfo&)` — cross-cell `TransferRequest` re-spawns route through the same overload, and audio there would retrigger the muzzle cue on every cell handoff. (The terrain-impact one-shot, by contrast, fires from `PostCollision` since impacts are not re-spawned.)

## See Also
- Parent collections: `../AGENTS.md`
