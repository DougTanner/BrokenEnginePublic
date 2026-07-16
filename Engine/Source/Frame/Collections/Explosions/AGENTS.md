# Explosions - Composite Effects

Composite explosion effects spawning lights, puffs, smoke trails, wind radials, and GPU particles. Compiles in both builds; visual spawning is client-only. Game code registers custom explosion types and spawns via `ExplosionsPostRender::Spawn(SpawnInfo)`.

## Unique Aspects

- **State lives in Interpolate, not PostRender**: atypical for a Collection — PostRender has zero SOA members and exists solely to host phase statics and the public `Spawn(SpawnInfo)` API. The placement lets client trail sync run during Interpolate with per-render-frame time; determinism is unaffected because CRC walks `SharedMembers()` only
- **Flags gate ownership**: `kDestroysSelf` controls auto-removal; without it the caller owns the row and only expired trails are reaped. Color flags tint GPU particles via channel bit-ORs
- **No own GPU pipeline**: all visible output comes from registered child collections (PointLights, Puffs, SmokeTrails, WindRadials) and `ParticleManager`; render phases only publish a CPU profile counter
- **Random-stream lockstep dominates `ExplosionsSpawn.cpp`**: per the hub determinism rule, every `common::Random*` call runs on both builds — results consumed only by client-only spawns are bound to `[[maybe_unused]]` locals outside the `#ifdef BT_CLIENT` blocks. The apparently dead server-side computation is load-bearing
- **Reconciliation guard**: `ParticleManager::Spawn` is skipped when `FrameFlags::kRecalculated` is set, preventing double-spawn during client reconcile; the Random calls feeding it still run (stream lockstep)
- **Fixed trail slots**: up to 8 owned SmokeTrails per row in parallel slot arrays; slots beyond the active count are zeroed every Update — stale data after swap-and-pop row reuse would desync the CRC'd trail times. Cleanup runs every frame (not only on parent expiry). Slot `j==0` is the central trail along the explosion direction; `j>0` are angle-jittered side trails carrying separate primary/secondary Length/Duration/Intensity `Wrapper` multipliers
- **Duration/length lockstep**: Spawn scales a trail's head travel distance by its Duration multiplier so that when Update later scales `pfTrailTimes` by the same multiplier, head speed stays constant — raising Duration extends space and time together instead of stalling the head into the smoke-decay window. Destroy intentionally reaps trails on *unmultiplied* `pfTrailTimes` so cleanup fires in lockstep with the shared explosion-row destruction; applying the client-only Duration multiplier there would orphan/leak the child SmokeTrail
- **Per-type particle `Wrapper*` overrides**: `ExplosionType` carries optional Wrapper pointers (null on server) applied multiplicatively once at spawn — tunes GPU particle size, speed, and intensity per source (missile vs. player vs. spaceship). Mirrors the hub's controller-keyframe Wrapper pattern but applies at spawn rather than per keyframe. `Register()` (default child types) is guarded idempotent
- **Type registry is static config, not state**: `ExplosionType` (a `TypeRegistry` entry) compiles identically on both builds but is never serialized or CRC'd; only the per-row SOA `SharedMembers()` subset participates in determinism (see hub for the member-split rule)
- **`SyncExplosionTrail` is deliberately not in the header**: defined in `Explosions.cpp` and shared with the other two `.cpp` files via repeated local forward declarations — visibility narrowing, not an oversight

## See Also
- `../AGENTS.md` - Collection\<T\>, SOA, Controller pattern, file-splitting convention
