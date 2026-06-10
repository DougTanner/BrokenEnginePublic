# /Engine/Source/Frame/Collections/Puffs/

Client-only fire-and-forget smoke puffs: stationary axis-aligned quads animated by keyframe controllers. Every puff spawns via `AddControlled()` — no plain `Add()` or owner-driven `Sync()` path. Types and controllers are registered by consumers (engine Explosions, game Players/Blasters), not here.

## Unique Aspects

- **Empty PostRender**: zero SOA members — exists only to satisfy the paired-collection protocol. Lifetime state (controller index, start time) lives on the Interpolate side; only `Destroy` has logic (delegates to `DestroyExpiredControlled`)
- **Custom keyframe/controller types** (`PuffKeyframe`/`PuffControllerType`): exist purely for semantically correct names (area/intensity/rotation vs. the light-centric generic `ControllerKeyframe`); the framework's templated registries and helpers accept them unchanged
- **Wrapper scaling is live, not baked**: only area and intensity carry per-keyframe `Wrapper*` multiplier arrays (rotation does not), re-applied every frame before interpolation — adjusting a UI wrapper retroactively changes alive puffs. Spawn applies keyframe-0 scaling so the first frame renders correctly
- **Selective copy**: `AllocateAndCopy` memcpys only type index and controller bookkeeping (controller index, start time); `Update` carries position over from the previous frame unchanged (puffs never move) and recomputes area/intensity/rotation from the controller
- **Delayed spawns**: passing a future start time to `AddControlled()` holds the puff at keyframe 0 until the delay elapses (negative elapsed time clamps) — used for secondary explosion puffs
- Render: quads culled via `IsPointVisible` then projected to base height; params pack intensity twice (straight multiplier plus the `pow(.y, fSmokeIntensityFalloff)` input in `Smoke.frag`) and rotation

## See Also
- [../CLAUDE.md](../CLAUDE.md) - Collection framework, Controller pattern
