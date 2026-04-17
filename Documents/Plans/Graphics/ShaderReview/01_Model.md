# Shader Review — Model

Files: `ModelGenBrdfLut.vert`, `ModelSkinned.vert`, `ModelStatic.vert`, `ModelShadow.frag`, `ModelGenBrdfLut.frag`, `Model.frag`

## PASS

- `Engine/Data/Shaders/Model/ModelGenBrdfLut.vert` — fullscreen quad; correct Vulkan Y-flip.
- `Engine/Data/Shaders/Model/ModelSkinned.vert` — uses precomputed `normalMatrix`, scalar layout, correct descriptor sets.
- `Engine/Data/Shaders/Model/ModelStatic.vert` — trivial dispatcher into `ModelVertexOutput`.
- `Engine/Data/Shaders/Model/ModelShadow.frag` — writes constant 0.0; no hazards.

## Minor notes

### `Engine/Data/Shaders/Model/ModelGenBrdfLut.frag`

- line 75 — `pow(x, 5.0)` with compile-time integer exponent; expand to multiplication chain (`x2 = x*x; Fc = x2*x2*x;`). Offline bake, impact negligible.

## NEEDS FIXES

### `Engine/Data/Shaders/Model/Model.frag`

Correctness:
- line 171 — `normalize(T - N*dot(N,T))` can divide by zero when T is parallel to N; det-guard at line 165 does not cover this case. Guard with `length(...) > 1e-6` or fall back to N.
- line 176 — `normalize(TBN * tangentNormal)` undefined when tangent-normal sample is exactly `(0.5,0.5,0.5)` → zero vector. Clamp base.
- line 266 — `fSunDot` computed but never read; dead code. Also uses unnormalized `f3InNormal` which would be a bug if used.
- lines 94, 99 — `pow(srgb, 2.2)` is the gamma-2.2 approximation, not true piecewise sRGB; if any bound color texture uses a `VK_FORMAT_*_SRGB` view, the GPU auto-linearizes and this double-decodes. Verify color textures are UNORM views.
- line 244 — `reflection.y *= -1.0;` followed by `ToCubemapCoord` Y/Z swap; net transform is non-obvious. Add a comment documenting the cubemap orientation convention.

Performance:
- line 130 — `pow(x, 5.0)` with compile-time exponent; expand to multiplies.
- lines 288-289, 301-302, 361, 365 — multiple `pow(vec3, vec3(uniform))` per fragment; uniform exponents prevent compiler lowering. Consider collapsing the diffuse/specular tuning knobs if shipping.
- line 315 — reimplements `ReadLighting()` from `ShaderFunctions.h` via C-style aggregate init; prefer the shared helper.

Recommendation: NEEDS FIXES — tangent construction guards (171, 176) and dead `fSunDot` are the actionable items.
