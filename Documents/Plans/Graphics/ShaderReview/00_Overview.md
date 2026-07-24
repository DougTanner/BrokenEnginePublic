# Shader Review — Overview

Manual reference/coordination document (no executable-plan metadata; never scheduler-tracked). Consolidates the findings of one Opus `/glsl-review` pass per shader file across all 60 first-party `.vert`/`.frag`/`.comp` files under `Engine/Data/Shaders/`. This overview is the sole artifact of that review: the per-subsystem plan files listed below were never created separately, and their findings live entirely in this file.

## Context

The review graded every first-party shader for NaN/Inf hazards, undefined behavior, synchronization, layout/binding discipline, and project-convention drift. The current tree has since absorbed most of the prescribed fixes; each finding below carries a verified status against the code as of 2026-07-24 so an implementer acts only on what remains open.

## Scope contract

In scope (target and ceiling — smallest complete change per item, nothing else):

1. `Engine/Data/Shaders/Lighting/AreaLight.frag` — in `main()`, only the texture-index conversion expression at line 51: `int32_t(f4InParams.x + 0.4f)`. Replace the `+ 0.4f` rounding bias with `+ 0.5f` round-to-nearest, or pass the index as a `flat` integer varying (which also touches only the matching varying declaration in this file and its producing vertex stage's matching declaration).
2. `Engine/Data/Shaders/Lighting/PointLight.frag` — in `main()`, only the identical conversion expression at line 52: `int32_t(f4InParams.x + 0.4f)`. Same fix as item 1.
3. `Engine/Data/Shaders/Wind/WindSpreadCommon.h` — only the decay expression at lines 67–68 (`fDecayRate` computation and `pow(fDecayRate, fTimeScale)`). Guard the pow base: `pow(max(fDecayRate, 0.0f), fTimeScale)`. `fDecayRate = 1.0 - mix(fWindDecayLow, fWindDecayHigh, fMagFactor)` goes negative if either decay slider exceeds 1.0; `pow` with a negative base and non-integer exponent is undefined.

Out of scope — everything else, including:

- Every finding marked **fixed** in the status tables below; do not re-touch those sites.
- All other lines of the three in-scope files, and all other shaders, headers, and C++ files.
- The "conventions worth codifying" section: documentation candidates only, not edits under this plan.
- Any refactor, helper extraction, abstraction, or fix to adjacent code encountered while editing.

Naming a file grants no permission beyond the named expressions plus the mechanical necessities (a varying declaration pair, if the `flat int` option is chosen) those changes require.

## Risk tier

Tier 2 — scoped shader behavior in the graphics subsystem. No determinism/CRC exposure (PostRender CRC excludes rendering), no wire/serialization/layout change unless the `flat int` varying option is chosen, in which case the CPU-visible vertex outputs stay untouched (varyings are shader-to-shader only). Changed shaders route through `/glsl-review`.

## Acceptance criteria

- Items 1–2: light textures still sample the same indices (bias 0.4 → 0.5 is value-preserving for the non-negative integer-valued floats currently passed; a diff review is decisive).
- Item 3: wind decay behavior unchanged for slider values ≤ 1.0; no NaN propagation when a decay slider exceeds 1.0. Diff review is decisive; no harness run required.
- All three touched shaders compile (shader build via `/compile` of the client target).

## Review summary (historical)

Summary as recorded at review time:

| Status | Files |
|---|---|
| PASS (no findings) | 21 |
| PASS with minor notes | 13 |
| NEEDS FIXES | 26 |

Per-subsystem grouping used by the review (file counts as of the review; the shader tree has since changed — current counts differ for Water, Terrain, Particles, Lighting, Smoke, Shadow, and Misc):

- Model (6), Water (2), Terrain (6), Particles (9), Quads (4), Lighting (8), HexShield/Objects (3), Smoke (4), Wind (4), Shadow (5), Misc — Log/Clear/DebugTexture (3)

## Cross-cutting findings — verified status

| # | Finding | Status in current tree |
|---|---|---|
| 1 | Billboard singularity on top-down camera — `cross(viewDir, worldUp)` degenerates when camera looks straight down | **Fixed.** `SquareParticlesRender.vert:55-58` perturbs world-up when view-parallel; `LongParticlesRender.vert:56-68` guards the cross candidate with an epsilon fallback |
| 2 | Unguarded `normalize()` on zero-length vectors | **Fixed** at the cited sites: `Model.frag:158-163` (Gram-Schmidt epsilon early-out), `Water.frag:233` (zero-weight-sum guard), `HexShieldLighting.frag:56-57` (pole guard on center xy), particle verts (see finding 1). `Shadow.comp:59` normalizes `vec3(fOtherX, 0, dz)` with `fOtherX != 0` on that path |
| 3 | Unguarded `pow(base, non-integer)` with possibly-negative base | **Mostly fixed:** `HexShield.frag:52,57`, `HexShield.vert:74`, `LightCombine.comp:45,67`, `DebugTexture.frag:48` all guard with `max(...)`; `LightingSpread.frag:112` base is clamped to [0,1] by construction. **Open:** `WindSpreadCommon.h:68` — in-scope item 3 above |
| 4 | Compute-shader implicit-LOD `texture()` undefined without derivatives | **Fixed.** `Shadow.comp`, `ObjectShadowsBlurH.comp`, `WindSpreadCommon.h`, `SmokeSpreadCommon.h` all use `textureLod(..., 0.0f)` |
| 5 | Missing `writeonly` on output `image2D` | **Fixed** at the cited sites: all Lighting/Shadow/ObjectShadows blur compute shaders and `Shadow.comp` carry `writeonly` |
| 6 | `ParticlesUpdate.comp` non-atomic RMW on allocation bitmap | **Fixed.** `ParticlesUpdate.comp:113` uses `atomicAnd` |
| 7 | `ParticlesSpawn.comp` dual-language type mismatch (`uint16_t` vs `uint8_t`) | **Fixed.** `ParticlesSpawn.comp:48-49` writes `uint16_t`, reconciled with the header (path still dead behind `ENABLE_32_BIT_BOOL`) |
| 8 | Shadow blur workgroup = 512 exceeds 32–256 range | **Fixed.** All Shadow compute shaders use `kiComputeTileSize` (8×8 = 64, `ShaderLayoutsBase.h:184`) |
| 9 | Descriptor-set discipline — compute shaders defaulting to set 0 | **Fixed.** Every Smoke/Wind/Lighting/Shadow `.comp` declares explicit `set = N` |

## Recurring conventions noted by the review — verified status

- Shared helpers `ReadLighting`, `SmokeShadow`, `LightingDepositEdgeFade` exist in `ShaderFunctions.h` (lines 108, 229, 279). The review claimed several fragment shaders reinvent them; not re-verified here — documentation/codification candidate only, out of scope.
- `gl_Position = vec4(0,0,-100,0)` w=0 vertex-kill pattern: **gone from the tree.** Particle verts now emit `vec4(0,0,0,1)` degenerate positions (`SquareParticlesRender.vert:40`, `LongParticlesRender.vert:40`).
- `int(flatFloatVarying + 0.4f)` rounding bias: **still present** as `int32_t(f4InParams.x + 0.4f)` at `AreaLight.frag:51` and `PointLight.frag:52` — in-scope items 1–2 above.
