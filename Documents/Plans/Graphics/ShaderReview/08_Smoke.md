# Shader Review — Smoke

Files: `Smoke.frag`, `SmokeOccupancyDilate.comp`, `SmokeSpreadOne.comp`, `SmokeSpreadTwo.comp`

## PASS

- `Engine/Data/Shaders/Smoke/Smoke.frag` — scalar layout, correct sets, `flat` integer varyings, uses `Rotate`.
- `Engine/Data/Shaders/Smoke/SmokeOccupancyDilate.comp` — workgroup 256, atomic growth of indirect dispatch X-count is intended.
- `Engine/Data/Shaders/Smoke/SmokeSpreadOne.comp` — barrier symmetry correct, 8×8=64 workgroup, shared memory properly initialized.
- `Engine/Data/Shaders/Smoke/SmokeSpreadTwo.comp` — correctness solid, symmetric barriers.

## Minor notes / soft recommendations

### `Engine/Data/Shaders/Smoke/Smoke.frag`

- line 31 — `pow(fMiscY, fSmokeIntensityFalloff)` can go +Inf if `fMiscY == 0` and falloff ≤ 0. If falloff is guaranteed positive by config, add a comment; otherwise `max(fMiscY, 1e-6)`.
- line 36 — hardcoded `/ 8` for tile size duplicates workgroup dim; use `kiComputeTileSize` from `ShaderFunctions.h`.
- line 31 — magic `0.0166666657f` (1/60) undocumented; introduce `const float kfFrameRateNorm = 1.0/60.0` or a comment.

### `Engine/Data/Shaders/Smoke/SmokeOccupancyDilate.comp`

- line 13 — `occupancyBuffer` read-only in this shader; add `readonly` qualifier.
- lines 40-53 — 5×5 neighborhood loop with `!bActive` short-circuit causes subgroup divergence; acceptable since dilate runs once per frame.

### `Engine/Data/Shaders/Smoke/SmokeSpreadTwo.comp`

- line 69 — `pow(fSmokeDecay, max(1.0f, fElevation - 5.0f))` is undefined if `fSmokeDecay <= 0`. Values are CPU-tuned; clamp `max(fSmokeDecay, 1e-6f)` or document the invariant.
- line 73 — edge decay multiplier could go negative if texel addressing changes; wrap in `max(0.0, ...)` defensively.
- line 86 — `atomicOr(suHasSmoke, 1u)` per thread; subgroup-reduce-then-one-atomic could cut contention (optional).
