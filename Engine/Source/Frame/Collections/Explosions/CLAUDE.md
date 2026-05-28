# Explosions - Composite Effects

Composite explosion effects spawning lights, puffs, smoke trails, wind radials, and GPU particles. Compiles in both builds; visual spawning is client-only. Game code registers custom explosion types.

## Unique Aspects

- **State lives in Interpolate, not PostRender**: atypical for a Collection — PostRender exists solely to host phase statics and the public `Spawn(SpawnInfo)` API
- **Flags gate ownership**: `kDestroysSelf` controls auto-removal; without it the caller owns the row and only expired trails are reaped. Color flags tint GPU particles via channel bit-ORs
- **No own GPU pipeline**: all visible output comes from registered child collections (PointLights, Puffs, SmokeTrails, WindRadials) and `ParticleManager`
- **Determinism in fire-and-forget spawns**: all `common::Random*` calls run unconditionally on both builds (unused results `[[maybe_unused]]` on server) so the random stream stays in sync when client-only visual spawns are skipped
- **Reconciliation guard**: `ParticleManager::Spawn` is skipped when `FrameFlags::kRecalculated` is set, preventing double-spawn during client reconcile
- **Per-explosion trail ring-buffer**: fixed slot count per row; slots beyond active count zero-initialized each Update. Cleanup runs every frame (not only on parent expiry). Slot `j==0` is the central trail along the explosion direction; `j>0` are angle-jittered side trails carrying separate primary/secondary Length/Duration/Intensity `Wrapper` multipliers
- **Duration/length lockstep**: Spawn scales a trail's head travel distance by its Duration multiplier so that when Update later scales `pfTrailTimes` by the same multiplier, head speed stays constant — raising Duration extends space and time together instead of stalling the head into the smoke-decay window. Destroy intentionally reaps trails on *unmultiplied* `pfTrailTimes` so cleanup fires in lockstep with the shared explosion-entry destruction; applying the client-only Duration multiplier there would orphan/leak the child SmokeTrail
- **Normalized keyframes + Wrappers**: keyframe values are multipliers; game-side `Wrapper` globals supply base magnitudes. `ExplosionType` carries optional per-type `Wrapper*` overrides for GPU particles — applied multiplicatively at spawn time to tune particle size, speed (velocity base/spread, decay, gravity), and visible-intensity (overall + spread/decay/power) per source (missile vs. player vs. spaceship). Mirrors the `ppVisibleAreaScales` controller-keyframe pattern but applies at spawn rather than per keyframe. `Register()` is idempotent
- **Type registry is static config, not state**: `ExplosionType` (a `TypeRegistry` entry) compiles identically on both builds but is never serialized or CRC'd; only the per-row SOA `SharedMembers()` subset participates in determinism (see hub for the member-split rule)

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection\<T\>, SOA, Controller pattern, file-splitting convention
