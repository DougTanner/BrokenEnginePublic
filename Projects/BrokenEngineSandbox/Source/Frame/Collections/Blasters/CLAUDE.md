# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/

Fast-moving energy projectiles with shared `BlasterType` configuration. Terrain impacts spawn visual and audio effects.

## Game-Specific Behavior

- **Motion & collision**: Constant-velocity linear integration with swept-sphere test (`Collision::AddLayer` with `kDestroyOnCollide`); destroyed on any hit. Direction is re-derived (renormalized from velocity) each interpolate tick, not integrated. Terrain intersection located via binary search with random jitter. The per-entity collision flag/radius/damage arrays are built in `PreCollision` into heap-suppressed thread-local vectors whose `.data()` must outlive `PostCollision`, so the workbuffer cannot back them.
- **Light type selection**: Each blaster type uses either an area light or a camera-aligned point light, chosen by a per-type index (`0xFF` sentinel = area light). Player blasters use area lights; enemy blasters use camera-aligned point lights registered in `Spaceships.cpp`. Invariant: exactly one of the per-entity area-light / point-light refs is valid; sync and teardown branch on which is present.
- **Impact effects**: Terrain impacts emit a crater point light, puff, and impact sound (client-only). Keyframes scale through game-side `LightingWrappers` / `SmokeWrappers`. Terrain-effect type/controller registration is idempotent (guarded by a `0xFF` sentinel), so `Register()` is safe to call repeatedly.
- **Pitch determinism**: `pfPitches` is seeded at Spawn from `rFrame.postRender.randomEngine` and is a shared (not client-only) field — keep the draw deterministic. It feeds the per-blaster looping sound, which is currently disabled (the `SoundsInterpolate::Sync` calls are commented out as placeholder audio); the sound object is still allocated/freed for lifetime parity, but no audio plays until a replacement clip is wired up.
- **Cross-cell transfer**: `TransferRequest::data` carries the wind-trail params under `#ifdef BT_CLIENT` so visuals survive a cell handoff.

## Spawning

Two overloads exist. The phase-dispatched `Spawn(Frame, FrameStaticData)` is intentionally empty — blasters have no server-driven spawner. New blasters come only from the `Spawn(Frame, SpawnInfo&)` overload invoked by weapon code. Do not add server-driven spawn logic to the phase entry point.

The fire-and-forget muzzle one-shot is emitted at the weapon firing site (Players/Spaceships combat code), NOT inside `Spawn(SpawnInfo&)`. This is deliberate: cross-cell `TransferRequest` re-spawns route through the same `SpawnInfo&` overload, and triggering audio there would retrigger the muzzle cue on every cell handoff. (The terrain-impact one-shot, by contrast, is fired from `PostCollision` since impacts are not re-spawned.) The per-blaster looping sound object is owned from `ClientInit` but is not currently synced (see Pitch determinism above).

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
