# Explosions - Composite Effects

Composite explosion effects spawning lights, puffs, smoke trails, wind radials, and GPU particles. Compiles in both builds; visual spawning is client-only. Game code registers custom explosion types.

## Unique Aspects

- **State lives in Interpolate, not PostRender**: atypical for a Collection — PostRender exists solely to host phase statics and the public `Spawn(SpawnInfo)` API
- **Flags gate ownership**: `kDestroysSelf` controls auto-removal; without it the caller owns the row and only expired trails are reaped. Color flags tint GPU particles via channel bit-ORs
- **No own GPU pipeline**: all visible output comes from registered child collections (PointLights, Puffs, SmokeTrails, WindRadials) and `ParticleManager`
- **Determinism in fire-and-forget spawns**: all `common::Random*` calls run unconditionally on both builds (unused results `[[maybe_unused]]` on server) so the random stream stays in sync when client-only visual spawns are skipped
- **Reconciliation guard**: `ParticleManager::Spawn` is skipped when `FrameFlags::kRecalculated` is set, preventing double-spawn during client reconcile
- **Per-explosion trail ring-buffer**: fixed slot count per row; slots beyond active count zero-initialized each Update. Cleanup runs every frame (not only on parent expiry)
- **Normalized keyframes + Wrappers**: keyframe values are multipliers; game-side `Wrapper` globals supply base magnitudes. `Register()` is idempotent
- **Type registry shared across builds**: particle fields carried on server (unused there) for CRC parity

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection\<T\>, SOA, Controller pattern, file-splitting convention
