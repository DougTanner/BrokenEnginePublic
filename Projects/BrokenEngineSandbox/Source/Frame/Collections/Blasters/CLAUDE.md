# /Projects/BrokenEngineSandbox/Source/Frame/Collections/Blasters/

Fast-moving energy projectiles with shared `BlasterType` configuration. Terrain impacts spawn visual and audio effects.

## Game-Specific Behavior

- **Motion & collision**: Constant-velocity linear integration with swept-sphere test; destroyed on any hit. Direction is re-derived (renormalized from velocity) each interpolate tick, not integrated. Terrain intersection located via binary search with random jitter.
- **Light type selection**: Each blaster type uses either an area light or a camera-aligned point light, chosen by a per-type index (`0xFF` sentinel = area light). Player blasters use area lights; enemy blasters use camera-aligned point lights registered in `Spaceships.cpp`. Invariant: exactly one of the per-entity area-light / point-light refs is valid; sync and teardown branch on which is present.
- **Impact effects**: Terrain impacts emit a crater point light, puff, and impact sound (client-only). Keyframes scale through game-side `LightingWrappers` / `SmokeWrappers`. Terrain-effect type/controller registration is idempotent (guarded by a `0xFF` sentinel), so `Register()` is safe to call repeatedly.
- **Pitch determinism**: `pfPitches` is seeded at Spawn from `rFrame.postRender.randomEngine` and is a shared (not client-only) field — keep the draw deterministic.
- **Cross-cell transfer**: `TransferRequest::data` carries the wind-trail params under `#ifdef BT_CLIENT` so visuals survive a cell handoff.

## Spawning

Two overloads exist. The phase-dispatched `Spawn(Frame, FrameStaticData)` is intentionally empty — blasters have no server-driven spawner. New blasters come only from the `Spawn(Frame, SpawnInfo&)` overload invoked by weapon code. Do not add server-driven spawn logic to the phase entry point.

## See Also
- Parent collections: [../CLAUDE.md](../CLAUDE.md)
