# Shader Footguns — Rationale and Sources

Detailed rationale for the scan items in `SKILL.md`. Read the item here when a review finding needs the "why" explained to the user, or when you want to cite a source.

## Table of Contents

- [Performance](#performance)
- [NaN / Inf Sources](#nan--inf-sources)
- [Precision Hazards](#precision-hazards)
- [Interpolation Qualifiers](#interpolation-qualifiers)
- [Coordinate-System & API Conventions](#coordinate-system--api-conventions)
- [Algorithmic / Math Mistakes](#algorithmic--math-mistakes)
- [Vulkan-GLSL-Specific](#vulkan-glsl-specific)
- [Broken Engine Rationale](#broken-engine-rationale)
- [Stages Not Yet Used in Repo](#stages-not-yet-used-in-repo)

---

## Performance

### Warp / wavefront divergence
Control flow inside `if`/`else` where the condition varies across a subgroup causes inactive lanes to stall — both paths execute, and the slow path dominates. Prefer `step()` / `mix()` / `clamp()` for small branches. Divergence that is *uniform* across the subgroup (push constants, per-draw values) is free. A subtle case: the hardware's implicit-LOD computation for `texture()` uses quad derivatives (2×2 pixel groups); if only 3 of 4 pixels in a quad take the branch, the LOD is undefined. [NVIDIA Nsight](https://developer.nvidia.com/blog/optimize-gpu-workloads-for-graphics-applications-with-nvidia-nsight-graphics/), [Maister on shader divergence](https://themaister.net/blog/2019/09/12/the-weird-world-of-shader-divergence-and-lod/).

### `discard` disables early-Z
When a fragment shader contains `discard` (or GLSL 460's `demote`), most hardware cannot commit the depth write until the shader finishes — which defeats hierarchical depth culling for the entire draw. Measurable cost: ~30% more fragment invocations on heavy scenes. Mitigations: put alpha-testing in a depth prepass; gate the draw by bounding volumes; or on Vulkan, use `VK_EXT_shader_demote_to_helper_invocation` where supported. [MJP on early-Z](https://therealmjp.github.io/posts/to-earlyz-or-not-to-earlyz/).

### Dependent texture reads
`texture(B, texture(A, uv).xy)` — the GPU cannot prefetch `B` until `A`'s result arrives. Whenever both reads exist in the same shader, the compiler must stall. Split across passes or precompute the indirection into a LUT.

### Dynamic loop bounds
`for (int i = 0; i < u_count; ++i)` where `u_count` is a uniform cannot be unrolled; every iteration pays branch overhead and blocks constant-folding. If the max is small, use a constant bound with an `if (i >= u_count) break;`.

### `inverse()` / `transpose()` in hot paths
`inverse(mat4)` expands to dozens of operations inlined; in a fragment shader it runs per-pixel. Precompute on the CPU (see `Engine/Data/Shaders/Model/ModelCommon.h`'s `normalMatrix` layout) and pass as an SSBO field. In this repo, `inverse()` is *also* banned outright — the NVIDIA compiler hangs on it. See Broken Engine Rationale below.

### `pow()` with integer exponent
`pow(x, 3.0)` compiles to `exp(3.0 * log(x))` — two transcendentals plus a multiply. `x * x * x` is one multiply. The compiler cannot always fix this automatically because `pow` must handle negative bases by rule.

### FP16 / `float16_t`
Desktop Vulkan with `VK_KHR_shader_float16_int8` lets you opt into 16-bit math. Turing issues FP16 at 2× rate; Ampere+ is closer to 1× but register pressure still drops, improving occupancy. Use for colors, normals, UVs. Do not use for depth, world positions, or iterative accumulators — error accumulates. [MJP on FP16 shaders](https://therealmjp.github.io/posts/shader-fp16/), [Vulkan 16-bit sample](https://docs.vulkan.org/samples/latest/samples/performance/16bit_arithmetic/README.html).

Note for this repo: no `mediump`/`highp`/`lowp` qualifiers are used — those are GLSL ES / WebGL concepts. Desktop GLSL 460 parses them but ignores them. Explicit `float16_t` from the extension is the only real way to reduce precision here.

### Shared memory bank conflicts (compute)
Most NVIDIA GPUs have 32 banks (4 bytes each); AMD varies. When threads in a warp access `shared` memory at stride-32 offsets, all requests hit the same bank and serialize. Fix by padding: `shared float tile[kTileX + 1][kTileY];`. Arm Mali uses 16-bank groups and wants 64-thread minimum workgroups. [Arm Mali compute guide](https://developer.arm.com/documentation/101897/0200/compute/shared-memory).

---

## NaN / Inf Sources

Any of these can produce NaN or Inf values that poison the entire color attachment. Once NaN enters a blend equation, the render target is dead until clear.

| Expression | Failure mode | Fix |
|---|---|---|
| `sqrt(x)` | NaN if `x < 0` | `sqrt(max(x, 0.0))` |
| `1.0 / x`, `a / x` | Inf if `x == 0` | `x / max(denom, eps)` |
| `normalize(v)` | undefined if `v == 0` (zero on most desktops, NaN on Apple Silicon) | `v / max(length(v), eps)` |
| `pow(x, y)` | NaN if `x < 0` and `y` non-integer | `pow(max(x, 0.0), y)` |
| `asin(x)`, `acos(x)` | NaN if `|x| > 1` | `asin(clamp(x, -1.0, 1.0))` |
| `log(x)`, `log2(x)` | Inf if `x == 0`, NaN if `x < 0` | `log(max(x, eps))` |
| `atan(0, 0)` | implementation-defined | early-out on zero |
| Normal Z: `sqrt(1 - x² - y²)` | NaN if `x² + y² > 1` | `sqrt(max(0.0, 1 - x² - y²))` |

Defensive late-stage: `any(isnan(color)) ? vec3(0) : color` as a last-resort backstop. Useful for shipping but not a substitute for fixing the source — the NaN propagated through multiple operations before the check. [WebGL normalize(0) issue #3697](https://github.com/KhronosGroup/WebGL/issues/3697), [SPIR-V GLSL.std.450 spec](https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html).

---

## Precision Hazards

### Catastrophic cancellation
`float32` has ~7 decimal digits. Subtracting two near-equal large floats leaves only the noise bits: `(1000000.1 - 1000000.0)` returns `0.125` on many GPUs, not `0.1`. If you compute differences of world-space positions far from origin, move into a local space first.

### Ordered accumulation
`sum += term` in a loop is order-dependent. If threads accumulate into a shared float via `atomicAdd`, the result is non-deterministic across runs. For deterministic sums: fixed-point atomics, or sort terms by magnitude, or Kahan summation.

### `invariant gl_Position`
Two draws to the same depth target must produce bit-exact depth or z-fight / pass-fail flicker occurs. `invariant gl_Position` in both vertex shaders disables fused-multiply-add reassociation across the vertex transform, guaranteeing bit-identical output. [GLSL 4.60 spec §4.6.1](https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html).

---

## Interpolation Qualifiers

- `smooth` (default) — perspective-correct interpolation across the triangle. Use for positions, UVs, colors, anything visible.
- `flat` — no interpolation; value from the provoking vertex. Mandatory for integer varyings (material IDs, instance indices) — silent linear interpolation of an integer-reinterpreted-as-float produces garbage indices.
- `noperspective` — linear in screen space. Rare; occasionally used for fullscreen-triangle UVs that shouldn't get perspective-divided.

Vertex output and fragment input qualifiers must match exactly or behavior is undefined. [Geeks3D tutorial](https://www.geeks3d.com/20130514/opengl-interpolation-qualifiers-glsl-tutorial/).

---

## Coordinate-System & API Conventions

Vulkan's conventions differ from legacy OpenGL. If you're porting a shader, these bite:

- **`gl_FragCoord` origin**: top-left in Vulkan, bottom-left in OpenGL. `gl_FragCoord.y == 0.5` is the top row.
- **NDC depth range**: `[0, 1]` in Vulkan, `[-1, 1]` in OpenGL. Projection matrices must target the right range.
- **NDC Y-axis**: Y-down in Vulkan. Either flip in the projection matrix (`proj[1][1] *= -1`) or flip `gl_Position.y` in the vertex shader.
- **Viewport**: Vulkan lets you flip by negative viewport height (`VK_KHR_maintenance1`) as an alternative.
- **sRGB**: sampling from an sRGB-format view auto-linearizes; writing to an sRGB attachment auto-gamma-corrects. UNORM views do neither. Apply gamma twice and everything looks washed; skip it and everything's muddy. Do math in linear, trust the format views.
- **`texture` vs `texelFetch`**: `texture(s, uv)` uses filtered UV sampling and needs derivatives; `texelFetch(t, ivec2(px), 0)` is unfiltered integer-pixel access, ignores samplers, always mip 0.

[Vulkan math foundations tutorial](https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Camera_Transformations/02_math_foundations.html).

---

## Algorithmic / Math Mistakes

### Lerp of unit vectors
`mix(N0, N1, t)` denormalizes. Specular highlights become dim/wrong until `normalize()` is reapplied in the fragment shader. For truly smooth rotation of directions, slerp (spherical interpolation) is correct but rarely needed in a frag shader — renormalization is the standard fix. [Lighthouse3D normalization](https://www.lighthouse3d.com/tutorials/glsl-12-tutorial/normalization-issues/).

### `reflect(unit, unit)` is already unit
GLSL `reflect(I, N) = I - 2*dot(N, I)*N` returns a unit vector when both `I` and `N` are unit. Magnitude-preserving sign tweaks include a leading `-` (`-reflect(...)`), a componentwise multiply by `vec3(±1, ±1, ±1)`, and a post-assignment single-axis flip on a named result (`vec3 r = reflect(...); r.y *= -1.0;`). All three forms keep magnitude at `1`. Wrapping the result in `normalize()` costs one `rsqrt + 3 muls` per fragment for nothing.

**Flag** `normalize(reflect(a, b))` (and the wrappers `normalize(-reflect(a, b))`, `normalize(reflect(a, b) * vec3(±1, ±1, ±1))`) when both `a` and `b` are demonstrably unit at the call site — the wrapper is a no-op. Also flag a `normalize()` applied to a named `reflect(...)` result that has only been sign-flipped (`r.x *= -1.0;` / `r.y *= -1.0;` / `r.z *= -1.0;`) since those flips are magnitude-preserving. Canonical relied-upon patterns: `Model.frag:253` (`-reflect(v, n)` consumed directly, no normalize) and `Water.frag`'s specular-lobe block (`vec3(-1, 1, -1) *` componentwise flip on the nested reflect).

**Also flag** *removal* of `normalize()` around `reflect(I, N)` when either input cannot be proven unit at the call site (sampled normals before renormalize, interpolated-then-not-renormalized varyings, sums of unit vectors that were never renormalized).

See `Engine/Data/Shaders/CLAUDE.md` `## Architecture Notes` for the convention bullet (*Unit-preserving identities*); family-specific call sites are documented in the child CLAUDE.md files (Water, Model, Objects).

### Matrix multiplication order
GLSL matrices are column-major by default. Post-multiply vectors: `vec4 clip = projectionMatrix * viewMatrix * worldPos;`. Swapping the order (`worldPos * matrix`) silently transposes the multiply and produces wrong results. `layout(row_major) mat4 M` flips the convention — use consistently across sharing sides.

### Phong vs Blinn-Phong
Phong: `pow(max(dot(reflect(-L, N), V), 0), shininess)`. Blinn-Phong: `pow(max(dot(N, H), 0), shininess)` where `H = normalize(L + V)`. Blinn-Phong is cheaper on modern hardware (no `reflect`), produces ellipse-shaped grazing-angle highlights closer to real rough-surface behavior, and generalizes cleanly to microfacet BRDFs (`H · N` *is* the microfacet normal). Phong highlights pinch circularly at grazing angles; its exponent must be roughly 2–4× Blinn's to match the same visual tightness. Full microfacet BRDFs (Cook-Torrance / GGX) dominate both when PBR fidelity matters — see `Model.frag` for the repo's Cook-Torrance + split-sum IBL implementation.

**Flag**: any specular implementation that uses `reflect()` + `dot(V, R)` in a new or modified shader. Report the tradeoff so the reviewer can decide: keep Phong (legacy match, simpler), switch to Blinn-Phong (cheaper + better grazing behavior), or adopt a microfacet BRDF (physically correct, matches `Model.frag`).

**Context for this repo**: `ShaderFunctions.h:90`'s `Specular` is Phong-shaped — `reflect(L, N)` then `dot(V, R)`, with an `exp2(power * log2(f))` fast-path summing three cos^N lobes. No shader calls it directly today: `Water.frag`'s specular block inlines its lobe math (wrapped in the `WATER_SPEC_AA_MODE` analytic-AA variants), passing a pre-reflected skybox direction rather than a surface normal — a multi-exponent cos^N lobe evaluator for skybox reflection. Active design docs (`Documents/Features/Graphics/ocean-phase-1-pbr-foundation.md:13`, `ocean-phase-tweaks-screen.md:183`) plan to replace this with a Ward BRDF. Include these facts in the review finding so the reviewer can weigh "rewrite now to Blinn-Phong" vs "wait for the Ward migration" — don't make the call for them.

### BRDF energy conservation
Diffuse + specular reflectance must sum to ≤ 1.0 at any direction. Naive Cook-Torrance can exceed this at grazing angles. Fix by weighting diffuse by `(1 - F)` where F is Fresnel. This repo's `Model.frag` implements Cook-Torrance with split-sum IBL — check that the Fresnel-driven diffuse weighting is still in place after any edit.

### Fresnel at grazing angles
`pow(1 - cosθ, 5)` where `cosθ → 0` is finite (→ 1). But `1 / (1 - cosθ)` blows up. Any custom Fresnel variant should be clamped: `pow(clamp(1.0 - cosθ, 0.0, 1.0), 5.0)`. [Lagarde PBR notes](https://seblagarde.wordpress.com/2011/08/17/hello-world/).

### Normal map Z reconstruction
Two-component normal maps store XY and reconstruct Z: `z = sqrt(1 - dot(xy, xy))`. If `dot(xy, xy) > 1` due to compression or filtering, `sqrt(negative) = NaN`. Use `sqrt(max(0.0, 1 - dot(xy, xy)))`.

### Shadow acne & PCF
Depth comparison without bias produces Moire-pattern self-shadowing. Slope-scaled bias: `bias * max(1.0, tan(acos(dot(N, L))))` or cheaper approximations. PCF kernels must be symmetric around the center pixel — a Poisson disk with non-symmetric offsets shifts the shadow edge. Test by flipping sign of every offset and confirming identical output.

### Tone mapping and gamma
Apply tone map to *linear HDR*, then either (a) write to an sRGB-format attachment (hardware gamma), or (b) write `pow(color, 1.0 / 2.2)` to a UNORM attachment. Never both. Never tone-map in gamma space. [LearnOpenGL HDR](https://learnopengl.com/Advanced-Lighting/HDR).

---

## Vulkan-GLSL-Specific

### std140 vs std430 vs scalar
- `std140` — strictest padding: vec3 aligned to 16 bytes, arrays stride 16.
- `std430` — tighter: vec3 still aligned to 16, arrays of scalars are 4-byte stride.
- `scalar` (`GL_EXT_scalar_block_layout`) — C-style tight packing, vec3 is 12 bytes, arrays of scalars are 4-byte stride.

**Apply the rule that matches the block's declared layout qualifier.** Under `layout(scalar)` the vec3-padding and 16-byte-stride rules are language-incorrect; under `std140` they are correct. The repo's global *uniform* default is `layout(scalar) uniform;` at `ShaderLayoutsBase.h:83`; SSBO blocks declare `scalar` explicitly per block (e.g., `Model/ModelCommon.h:40`) or via a per-shader `layout(scalar) buffer;` (e.g., `Ui/UiDepthPrepass.vert:4`). An unqualified buffer block defaults to std430 — that is the silent mismatch to catch. Flag any introduction of `std140`/`std430` because it contradicts the repo's shared-struct-layout contract (C++ DirectXMath types assume scalar packing). [Vulkan shader memory layout](https://docs.vulkan.org/guide/latest/shader_memory_layout.html).

### Push constants
Spec minimum 128 bytes, most drivers 256. Declare once per shader using `layout(push_constant) uniform PC { ... };`. If multiple stages need the same struct, share it. Over-sized pushes fail pipeline creation.

### Specialization constants
`layout(constant_id = N) const int kFoo = 0;` bakes into SPIR-V at pipeline-create. Cheaper than a uniform for branch-gated features, but each distinct value needs its own pipeline object. Use for feature toggles, not per-frame data.

### Subgroup operations
`subgroupBallot`, `subgroupBroadcast`, `subgroupAdd`, etc. require `VK_KHR_shader_subgroup_*` features enabled on the device. The shader compiler accepts them regardless; the driver fails silently or corrupts data at runtime if the feature isn't on. Document the feature requirement next to every use.

### `gl_Layer` / `gl_ViewportIndex`
In a vertex shader they require `shaderOutputLayer` / `shaderOutputViewportIndex` features. In a fragment shader they're always readable. Without the features, writes are dropped.

### `robustBufferAccess`
Prevents OOB reads from *crashing* — but the return value is unspecified (zero or garbage), not safe. Shaders must still bounds-check indices. [Vulkan spec on robustBufferAccess](https://www.khronos.org/registry/vulkan/specs/1.2/html/vkspec.html#features-robustBufferAccess).

---

## Broken Engine Rationale

### Why the `inverse()` ban
Discovered in production: NVIDIA's shader compiler hangs indefinitely during `vkCreateGraphicsPipelines` when a shader contains `inverse(mat3)` or `inverse(mat4)`. The hang is inside the driver, pre-dispatch — no timeout, no error. Workaround: precompute the inverse on the CPU and upload via a buffer. `Engine/Data/Shaders/Model/ModelCommon.h:50` shows the canonical pattern (normal matrix as `vec4[3]`). This is a hard flag, zero exceptions — even a dead code path with `inverse()` triggers the hang.

### Why scalar block layout
The repo shares struct definitions between C++ (DirectXMath types) and GLSL (vec/mat types) via `#if defined(BT_ENGINE)` switching at `ShaderLayoutsBase.h:10` — the C++ build defines `BT_ENGINE`, the GLSL compile does not. Scalar layout matches C/C++'s natural packing rules, so the same struct declaration works on both sides with no explicit padding fields. std140 would force GLSL-side padding that the C++ side doesn't have, silently corrupting the CPU/GPU data pipeline. The DataPacker invokes `spirv-opt --scalar-block-layout` after `glslangValidator` — see `DataPacker/Source/ExportJobs/ExportShader.cpp:314`.

### Why the descriptor set convention
- **Set 0** is bound rarely (once per frame) — globals that every pipeline reads.
- **Set 1** is bound per-pipeline — pipeline-specific storage.
- **Set 2** is bound per-draw — only models use it today.

Binding a descriptor set invalidates all higher-numbered sets on some drivers, so heavier-changing bindings live at higher indices. Mixing roles (e.g., per-material data in set 0) forces unnecessary set 0 rebinds every draw.

### Why `nonuniformEXT` on bindless indices
Without `nonuniformEXT`, the compiler assumes the index is uniform across the subgroup, which lets it use a single descriptor fetch. If the index actually diverges, the result is undefined — often the "right" texture for lane 0 sampled in every other lane. Marking the index `nonuniformEXT(i)` tells the driver to emit divergent-descriptor handling. Cheap when the index truly is uniform; necessary when it isn't.

---

## Stages Not Yet Used in Repo

The repo currently uses only vertex, fragment, and compute stages. These checks apply if a new shader of another stage is added — otherwise ignore them during review.

### Geometry (`.geom`)
- Emitting more vertices than the `layout(max_vertices = N)` declaration silently truncates. Count emit paths carefully.
- Geometry stage has serialization cost on most desktop GPUs; consider whether a compute shader + indirect draw would be cheaper before adding one.

### Tessellation Control / Evaluation (`.tesc` / `.tese`)
- `gl_in.length()` must match the input patch topology declared on the C++ side (`VK_PRIMITIVE_TOPOLOGY_PATCH_LIST` + `VkPipelineTessellationStateCreateInfo::patchControlPoints`).
- Tess-control outputs are per-output-vertex; tess-eval inputs are per-patch plus interpolation by `gl_TessCoord`. Confusing the two produces garbled geometry.

### Mesh / Task (`.mesh` / `.task`)
- Requires `VK_EXT_mesh_shader` or `VK_NV_mesh_shader`. Check the feature flag is enabled before adding.
- Output vertex/primitive counts hard-capped by device limits (`maxMeshOutputVertices`, `maxMeshOutputPrimitives`).

### Ray Tracing (`.rgen`, `.rmiss`, `.rchit`, `.rahit`, `.rint`, `.rcall`)
- Requires `VK_KHR_ray_tracing_pipeline` + `VK_KHR_acceleration_structure`.
- Shader binding table (SBT) layout must match the pipeline's hit/miss/callable group ordering exactly.
- `rayPayloadEXT` / `hitAttributeEXT` types must match across traced pipelines.
