# Shaders - Vulkan GLSL Shader Source

## Overview

GLSL shader source for the Vulkan 1.2 pipeline, compiled to SPIR-V by the DataPacker. Shaders share data structure definitions with C++ through dual-language headers that preprocessor-switch between DirectXMath and GLSL types.

## Architecture Notes

- **Scalar block layout**: All uniform and storage buffers use `GL_EXT_scalar_block_layout` with a global `layout(scalar) uniform;` directive. Struct members are tightly packed with C++ alignment rules (no std140 padding), keeping CPU/GPU layouts in sync. DataPacker passes `--scalar-block-layout` to spirv-opt. Plain `float[]` arrays are 4-byte stride, not 16-byte.
- **Dual-language headers**: `ShaderLayoutsBase.h` preprocessor-switches between DirectXMath (C++) and GLSL vec types so the same struct definition serves both sides. Shader-wide scalar constants live here too; naming follows `kf` for floats, `ki` for ints, `ke` for enum-like / format constants, `kb` for bools.
- **Bindless textures**: Unsized `texture2D[]` arrays with separate samplers and `nonuniformEXT()` dynamic indexing.
- **Multi-set descriptors**: Set 0 = global (UBOs, samplers, bindless textures), Set 1 = per-pipeline (SSBOs, combined image samplers), Set 2 = per-material (models only).
- **Push-constant render modes**: Vertex shaders select camera/visible-area/shadow projection without separate pipeline permutations.
- **Four-channel EWNS directional lighting**: RGB stored as separate render targets with EWNS directional weights. Rationale: directional and ambient EWNS samples are summed first, then passed together through the normal-weighted path so both contributions are normal-weighted consistently.
- **World-space directional deposit**: Deposit shaders compute EWNS direction weights from the fragment's world-space offset to the light's center using cos^2 lobe weighting, with an epsilon fallback to omnidirectional near the center.
- **`reflect(unit, unit)` is unit by identity**: GLSL `reflect(I, N) = I - 2*dot(N,I)*N` preserves unit length when both inputs are unit vectors (algebraic proof: `|reflect|^2 = |I|^2 - 4(N.I)^2 + 4(N.I)^2*|N|^2 = 1`). Callers that pre-normalize both inputs may consume the result directly as a direction with no outer `normalize()`. A leading sign flip (`-reflect(...)`) or a single-axis componentwise sign flip (`reflect(...) * vec3(+/-1, +/-1, +/-1)`, or `reflection.y *= -1.0`) preserves unit length too — these are sign changes, not magnitude changes. Removing the redundant `normalize()` saves one `rsqrt + 3 muls` per fragment per call site. Currently relied on at:
	- `Water/Water.frag:203` — skybox sample `textureLod(skyboxSampler, -reflect(f3ToEyeNormal, f3SkyboxWaveNormal), ...)`; `f3SkyboxWaveNormal` normalized at `:202`.
	- `Water/Water.frag:208` — specular reflection vector passed into `Specular(...)`.
	- `Water/Water.frag:258` — `reflect(f3EyeToPoint, f3ReflectedNormal)` for the base-height reflected sample; `f3ReflectedNormal` normalized at `:255`, `f3EyeToPoint` at `:257`.
	- `Objects/HexShield.frag:40` — `reflect(f3IncidentNormal, normalize(f3InCenterNormal))` for skybox sample; `f3IncidentNormal` normalized at `:39`.
	- `Model/Model.frag:252` — `-reflect(v, n)` fed into `GetIBLContribution`, which then swizzles via `ToCubemapCoord` for the Y-up Kloofendal IBL cubemaps; `v` normalized at `:249`, `n = GetNormal(...)` is unit on every return path (each ends in an explicit `normalize(...)`).
	- `ShaderFunctions.h:85` — `reflect(f3LightNormal, f3Normal)` inside the `Specular(...)` helper; both parameters typed as direction normals and all callers pass pre-normalized vectors.
- **`cross(unit, unit)` of perpendicular unit vectors is unit**: `|cross(a, b)| = |a||b|sin(theta)`; when both inputs are unit and orthogonal, the result is unit. Removing the redundant outer `normalize()` saves one `rsqrt + 3 muls` per call site. Currently relied on at:
	- `Particles/SquareParticlesRender.vert:58` — `f3UpNormal = cross(f3LeftNormal, f3ToEyeNormal)`; world-up is ternary-perturbed at `:55` when `|forward.z| > 0.999`, so `f3LeftNormal = normalize(cross(f3ToEyeNormal, f3WorldUp))` at `:56` is unit and orthogonal to `f3ToEyeNormal`.
	- `Model/Model.frag:180` — `B = cross(N, T)` after Gram-Schmidt orthogonalisation at `:173-178`; `N` is unit on every return path and `T = normalize(Tperp)` at `:178` is unit and orthogonal to `N`.

## Known Issues

**NVIDIA driver bug**: Do NOT use `inverse()` on mat3/mat4 in shaders. NVIDIA's compiler hangs indefinitely during pipeline creation. Precompute inverse matrices on the CPU and pass via buffers.

## See Also

- [Debug/CLAUDE.md](Debug/CLAUDE.md)
- [Lighting/CLAUDE.md](Lighting/CLAUDE.md)
- [Quads/CLAUDE.md](Quads/CLAUDE.md)
- [Model/CLAUDE.md](Model/CLAUDE.md)
- [Objects/CLAUDE.md](Objects/CLAUDE.md)
- [Particles/CLAUDE.md](Particles/CLAUDE.md)
- [Shadow/CLAUDE.md](Shadow/CLAUDE.md)
- [Smoke/CLAUDE.md](Smoke/CLAUDE.md)
- [Terrain/CLAUDE.md](Terrain/CLAUDE.md)
- [Water/CLAUDE.md](Water/CLAUDE.md)
- [Wind/CLAUDE.md](Wind/CLAUDE.md)
