# Shader Review — Particles

Files: `Billboards.vert`, `LongParticlesRender.vert`, `SquareParticlesRender.vert`, `LightingParticlesRender.vert`, `Billboards.frag`, `ParticlesRender.frag`, `LightingParticlesRender.frag`, `ParticlesSpawn.comp`, `ParticlesUpdate.comp`

## PASS

- `Engine/Data/Shaders/Particles/Billboards.vert` — scalar-layout SSBO, correct descriptor discipline, uses shared `Rotate` helper.
- `Engine/Data/Shaders/Particles/LightingParticlesRender.vert` — scalar layout, correct sets, `flat` instance index, correct Vulkan Y-flip.
- `Engine/Data/Shaders/Particles/Billboards.frag` — correct bindless `nonuniformEXT`, scalar layout.
- `Engine/Data/Shaders/Particles/LightingParticlesRender.frag` — atomic tile-occupancy marking is race-safe; `LightingDepositEdgeFade` used correctly.

## NEEDS FIXES

### `Engine/Data/Shaders/Particles/LongParticlesRender.vert`

Correctness:
- line 44 — `normalize(f4Velocity.xyz)` NaNs on zero-velocity particles (newly spawned / stalled).
- line 53 — `normalize(cross(f3ToEyeNormal, f3Direction))` NaNs when velocity parallel to view direction (top-down camera + vertically-moving particle).
- line 57 — division by `(fParticlesStretchVelocityEnd - fParticlesStretchVelocityStart)` has no guard.
- line 35 — `1 << (i % 32)` in signed int; `1 << 31` is UB. Use `1u << (uint(i) % 32u)`.

Performance:
- line 56 — `length(velocity)` recomputes what line 44 normalized; hoist length once.

### `Engine/Data/Shaders/Particles/SquareParticlesRender.vert`

Correctness:
- line 46 — `normalize(eye - center)` NaN if particle at eye.
- line 50 — **`cross(f3ToEyeNormal, vec3(0,0,1))` produces zero when view direction is parallel to world up — i.e. the stated top-down RTS camera**. Subsequent `normalize` NaNs. Guard with epsilon fallback or derive right axis from view matrix.

Performance:
- line 51 — `normalize(cross(unit, unit))` of perpendicular unit vectors is already unit; drop.
- lines 45, 54, 65 — three separate SSBO reads of `pParticles[i]` fields; hoist `ParticleLayout p = ...` once.

### `Engine/Data/Shaders/Particles/ParticlesRender.frag`

Correctness:
- line 36 — `pow(fIntensity, ...)` with potentially-negative base and non-integer exponent is undefined. Clamp: `pow(max(fIntensity, 0.0), ...)`.
- line 40 — output alpha hardcoded `0.0f`; only correct if the render state is additive pre-multiplied. Verify or comment.

### `Engine/Data/Shaders/Particles/ParticlesSpawn.comp`

Broken Engine:
- lines 51, 94 — shader writes `uint8_t(0)`/`uint8_t(1)` to `pbAllocated[]`, but `ShaderLayoutsBase.h:678` declares it as `uint16_t pbAllocated[]`. Under scalar layout the GLSL side assumes 1-byte stride while C++ sees 2-byte. Path is currently dead behind `ENABLE_32_BIT_BOOL` but reconcile.

Performance:
- line 6 — `local_size_x = 1` is below 32-lane subgroup threshold. Algorithm is serial (sequential free-list scan) so parallelism can't help directly; add a comment justifying, or refactor to a subgroup-ballot claim-slot scheme.
- lines 46-53 — full-buffer clear loop on a single thread during reset; consider `vkCmdFillBuffer` or a parallel clear dispatch instead.

Vulkan/API:
- line 107 — `debugPrintfEXT` fine for dev; strip before shipping.
- line 8 — missing explicit `set = 0` vs sibling particle shaders.

### `Engine/Data/Shaders/Particles/ParticlesUpdate.comp`

Correctness:
- line 100 — non-atomic `puiAllocated[i/32] &= ~(1u << (i%32))`: 32 neighboring threads share the word; concurrent deallocations race and can resurrect or lose free bits. **Fix: `atomicAnd(puiAllocated[i/32], ~(1u << (i%32)))`**.
- line 51 — `f4Position += fDeltaTime * f4Velocity` integrates `.w` too; if `.w` carries state this is wrong (benign if pure padding).
- line 58 — terrain-collision branch re-integrates a full `dt` step after flipping `z`, advancing `2×dt` in `xy`. Use half-step or reflect-from-penetration.
- line 95 — death test uses pre-collision `f4Position.z`; else-branch writes a new position but the test reads the old. Use the written position.

Performance:
- lines 43-97 — many per-iteration `pParticles[i]` reloads; hoist `ParticleLayout p = pParticles[i]; ... pParticles[i] = p;`.
- line 106 — `atomicMin` contention when many particles die in the same frame; subgroup-reduce before the atomic would cut traffic.
