---
name: glsl-review
description: Reviews GLSL shader changes (.vert .frag .comp and related stages) for correctness, performance, and Broken Engine conventions. Use this skill after editing shader source under Engine/Data/Shaders/ or Projects/*/Data/Shaders/ to catch NaN/Inf hazards, divergent branching, early-Z regressions, descriptor-set mistakes, scalar-block-layout violations, and the NVIDIA `inverse()` compiler-hang bug. ALSO use proactively when the user asks to review, audit, or verify shader code.
allowed-tools: [Read, Grep, Glob, WebFetch]
paths: ["**/*.vert", "**/*.frag", "**/*.comp", "**/*.geom", "**/*.tesc", "**/*.tese", "**/*.mesh", "**/*.task", "**/*.rgen", "**/*.rmiss", "**/*.rchit", "**/*.rahit", "**/*.rint", "**/*.rcall", "**/*.glsl", "**/Data/Shaders/**/*.h"]
---

# GLSL Shader Review

Reviews GLSL shader changes for **correctness** (NaN/Inf, wrong math, wrong precision, coordinate/space confusion), **performance** (divergence, early-Z, dependent fetches, dynamic loops, hot-path `inverse()`), and **Broken Engine conventions** (scalar block layout, descriptor sets, bindless indexing, NVIDIA `inverse()` ban).

Sibling to `/repo-code-review` — that skill covers C++; this one covers shaders. Style/formatting is out of scope.

**Principle — no suppression rules.** Describe invariants and flag deviations; never instruct reviewers to ignore specific existing code. "Don't flag X — it's deliberate / legacy / slated for replacement" goes stale the moment the code changes or gains callers, and hides legitimate findings. When an implementation is a known tradeoff, report the tradeoff (e.g., "Phong rather than Blinn-Phong — cheaper-to-rewrite vs. wait-for-Ward-migration") and let the reviewer decide. Parser-level carve-outs (e.g., ignoring `inverse(` inside comments) are fine — they prevent false positives, not real findings.

## Instructions

### 1. Identify Modified Shaders

Scan the conversation for shader files edited in this session — any `.vert`, `.frag`, `.comp`, `.geom`, `.tesc`, `.tese`, `.mesh`, `.task`, ray-tracing stages (`.rgen` etc.), `.glsl`, and shared headers under `Engine/Data/Shaders/**/*.h` or `Projects/*/Data/Shaders/**/*.h`. Include `ShaderLayoutsBase.h`, `ShaderLayouts.h`, `ShaderFunctions.h`, and any `*Common.h` that shaders `#include`.

For each modified file, focus the review on the *changed* regions, but always skim the whole file for nearby interactions that the change may have broken.

---

### 2. Broken Engine Rules (hard flags — project-specific)

These are repo conventions enforced by the pipeline itself. Any violation is a bug, not a style opinion.

#### 2a. NVIDIA `inverse()` hang

**Never call `inverse()` on `mat3`/`mat4` inside any shader.** NVIDIA's compiler hangs indefinitely during pipeline creation when it encounters this. Precompute the inverse on the CPU and pass it in via a buffer.

Reference pattern — see `Engine/Data/Shaders/Model/ModelCommon.h:50`, which stores a CPU-precomputed normal matrix as `vec4 normalMatrix[3]` (transpose(inverse(mat3(matrix))), stored as three `vec4`s).

Flag any call site: `inverse(` followed by a variable or expression in shader code. Ignore occurrences inside comments and string literals — the canonical reference pattern's comment at `ModelCommon.h:50` legitimately contains the text `transpose(inverse(mat3(matrix)))`.

#### 2b. Scalar block layout only

Every UBO/SSBO in this repo uses scalar layout. `Engine/Data/Shaders/ShaderLayoutsBase.h:83` sets the global uniform default (`layout(scalar) uniform;`); SSBO blocks declare `scalar` explicitly per block (e.g., `Model/ModelCommon.h:40`) or via a per-shader `layout(scalar) buffer;`. Flag any `std140` or `std430` qualifier added to shader code, and any new SSBO block with no layout qualifier at all — unqualified buffer blocks default to std430, silently mismatching the scalar-packed C++ struct. The DataPacker passes `--scalar-block-layout` to `spirv-opt`; mismatched layout qualifiers silently break CPU/GPU struct alignment.

Corollary: **plain `float[]` arrays are 4-byte stride**, not 16-byte. Flag shared C++/GLSL structs that add explicit 4×float-aligned padding after each float array element — that padding is dead under scalar layout and indicates the author expected std140 rules.

#### 2c. Descriptor set discipline

The engine fixes set indices by role:

- **Set 0** — global
- **Set 1** — per-pipeline
- **Set 2** — per-material (models only today)

Flag any `layout(set = N, binding = M)` where the role doesn't match the set number — e.g., a per-material texture declared in set 0, or a pipeline SSBO declared in set 2.

#### 2d. Bindless textures require `nonuniformEXT`

Bindless textures are declared as unsized arrays with separate samplers (`texture2D myTextures[]`). **Any dynamic index that is not a compile-time constant must be wrapped in `nonuniformEXT(...)`.** Missing this gives undefined behavior on most drivers and artifacts that only show up on specific hardware. Flag any `myTextures[someVariable]` or `sampler2D(myTextures[i], mySampler)` where `i` is not trivially uniform.

#### 2e. Dual-language header sync

When `ShaderLayoutsBase.h`, `ShaderLayouts.h`, or any `*Common.h` struct is modified, the C++ side sees it through DirectXMath types and the GLSL side sees it through `vec`/`mat` types via `#if defined(BT_ENGINE)` switching (see `Engine/Data/Shaders/ShaderLayoutsBase.h:10`). Flag:

- A struct field added/removed/reordered on one side of the `BT_ENGINE` guard but not the other.
- A type whose C++ and GLSL representations differ in size/alignment under scalar layout (e.g., `XMFLOAT3` vs `vec3`, array stride assumptions).
- `#include` of the header from a C++ file that isn't also in the DataPacker's `.d` depfile path — stale caches won't rebuild. See `ExportShader::CheckDirty` in `DataPacker/Source/ExportJobs/ExportShader.cpp` for the `.d`-depfile parse path.

#### 2f. Prefer shared helpers in `ShaderFunctions.h`

Before flagging a correctness issue, check whether `Engine/Data/Shaders/ShaderFunctions.h` already provides the utility being reinvented — transforms and projection (`Rotate`, `Transform`, `WorldToVisibleArea`, `VisibleAreaToWorld`), lighting (`SunLighting`, `Specular`, `ReadLighting`, `IntensityLighting`, `DirectionalLighting`, `WaterLighting`, `AmbientLighting`), smoke (`SmokeShadow`, `AddSmoke`, `BlendSmoke`), `LightingDepositEdgeFade`, and more. The header gains helpers over time; skim it for the full list. If a new shader re-implements one by hand, flag it and recommend the shared version.

---

### 3. Correctness Footguns (universal — shader semantics)

Walk the changed code against this list. The detailed rationale for each item lives in `references/shader-footguns.md` — read that if a finding needs extra context or you need to explain "why" in the review report.

#### NaN / Inf sources

Flag any of the following without a guard (`max`, `clamp`, or an explicit epsilon):

- `sqrt(x)` where `x` could be negative
- `1.0 / x`, `a / x` where `x` could be zero
- `normalize(v)` where `v` could be the zero vector — use `length(v) > epsilon` guard or `v / max(length(v), 1e-6)`
- `pow(x, y)` where `x` could be negative and `y` is non-integer (undefined; many drivers return NaN)
- `asin(x)` / `acos(x)` without `clamp(x, -1.0, 1.0)`
- `log(x)`, `log2(x)` where `x` could be `<= 0`
- `atan(y, x)` at `(0, 0)` — implementation-defined
- Reconstructing a normal's Z as `sqrt(1 - dot(xy, xy))` without `max(0, ...)`
- Fresnel at grazing angles producing unbounded values (clamp the `pow` base)

The defensive pattern `any(isnan(color))` / `any(isinf(color))` is acceptable as a late-stage guard but should not substitute for fixing the source.

#### Precision hazards

- **Subtracting near-equal large floats** (e.g., two world-space positions far from origin) — catastrophic cancellation. Move the subtract into a space closer to origin first.
- **Accumulating many small contributions into a float** — use Kahan summation or sort by magnitude if the count is large and order matters.
- **Depth-comparison equality** — if a depth or position must exactly match across two passes, both writers must share the same expression and be qualified `invariant`.

No precision qualifiers (`mediump`, `highp`, `lowp`) should appear in this repo — desktop Vulkan GLSL 460 ignores them. Flag any new ones as dead code.

#### Interpolation qualifiers

- Integer varyings, instance/material IDs, and flags must be `flat`. Silent linear interpolation of an ID produces nonsense indices.
- Screen-space attributes that must not perspective-correct (e.g., post-process UVs written from a fullscreen triangle) should be `noperspective`.
- `invariant gl_Position` when two pipelines must produce identical depth.

#### Coordinate-system and API conventions

Vulkan differs from legacy OpenGL. Flag:

- Code assuming `gl_FragCoord` origin is bottom-left (Vulkan is top-left unless overridden).
- NDC depth range assumed `[-1, 1]` — Vulkan is `[0, 1]`.
- Missing Y-flip in clip space when porting an OpenGL shader.
- sRGB vs linear color space: samples from sRGB-view textures are auto-linearized; samples from UNORM views are not. Flag gamma applied twice or not at all.
- Premultiplied vs straight alpha mixed in the same blend equation.
- `texture()` vs `texelFetch()` confusion — `texelFetch` takes integer pixel coords and ignores samplers; `texture` uses filtered UV sampling.

#### Math / algorithm mistakes

Catch these by reading the intent, not pattern-matching:

- Lerping two unit vectors and using the result without renormalizing.
- Lerping directions/quaternions that should be slerped.
- Column-major vs row-major matrix multiply order (`v * M` vs `M * v`) — GLSL defaults to column-major.
- Blinn-Phong written as `pow(dot(H, N), n)` using the wrong half-vector (`H = normalize(L + V)`, not `L + N`).
- BRDF energy not conserved (diffuse + specular > 1 somewhere).
- Normal-map Z reconstruction without `max(0, ...)`.
- Shadow bias sign wrong for the depth convention.
- Tangent-space normal applied in world space or vice versa — this repo uses `dFdx`/`dFdy` tangent reconstruction in `Model.frag`; verify consistency.

---

### 4. Performance Footguns

#### 4a. Hot-path cost

Flag in fragment or inner compute loops:

- `inverse()` anywhere (see §2a — but worth restating: it's a NaN footgun *and* a driver hang *and* a perf disaster).
- `pow(x, k)` where `k` is a compile-time positive integer — expand to multiplications.
- `pow(x, k)` in a `for` loop with a non-constant `k` — the compiler can't lower it.
- Trig (`sin`/`cos`/`tan`/`asin`/`acos`/`atan`) inside per-fragment or inner-loop code where a polynomial approximation or LUT would do.
- Per-fragment `length(v)` when `dot(v, v)` (squared) suffices for a comparison.
- `normalize()` applied to a vector that's already normalized.
- `normalize()` around `reflect(I, N)` when both `I` and `N` are unit at the call site — `reflect(unit, unit)` is unit by identity, and sign-flip wrappers (leading `-`, componentwise `vec3(±1, ±1, ±1)` multiply, single-axis flip) preserve magnitude. Conversely, flag *removal* of `normalize()` around `reflect(...)` when either input cannot be proven unit at the call site. Details and current call sites: *Algorithmic / Math Mistakes → `reflect(unit, unit)` is already unit* in `references/shader-footguns.md`.

#### 4b. Divergence and early-Z

- **`discard`** (and the `demote` keyword from `GLSL_EXT_demote_to_helper_invocation`) disables early-Z on most hardware for the whole draw. If the draw has a depth prepass or the fragment is expensive, flag `discard` and suggest moving the alpha-test into the prepass or using `VK_EXT_shader_demote_to_helper_invocation` with care.
- **Divergent branching** on values that vary across a subgroup — particularly dynamic UV sampling inside an `if` block — causes both paths to execute. For pure-uniform branches (push constants, per-draw), divergence is free.
- **`texture()` sampling inside `if`/`for`** needs implicit-LOD derivatives; undefined behavior if a subgroup has disagreeing control flow. Use `textureLod` / `textureGrad` inside dynamic branches.

#### 4c. Memory access patterns

- **Dependent texture reads**: sampling a texture with a UV that was itself produced by another texture read — prefetch-hostile, slow. Flag if a simpler formulation exists.
- **SSBO read inside a hot loop** when the value could be hoisted to a uniform or a local.
- **Compute shared-memory bank conflicts**: consecutive threads writing to addresses `N*kStride` where `kStride % 32 == 0` — flag and recommend padding.
- **`memoryBarrier*` at the wrong scope**: `memoryBarrierShared` for inter-workgroup data doesn't work; use `memoryBarrierBuffer` + `barrier()` at the right place.
- **Workgroup size** too small (<32) wastes lanes on desktop; too large (>256) hurts occupancy on some hardware. Flag `local_size_x * local_size_y * local_size_z` that falls outside `[32, 256]` without a comment explaining why.

#### 4d. `imageStore` / `imageLoad` correctness

- Writes to the same image from multiple invocations without `coherent` + barriers race.
- `readonly` / `writeonly` qualifiers missing where the shader only reads or only writes — loses driver optimizations.

---

### 5. Vulkan-Specific Footguns

- **Push constants** are capped (128 bytes guaranteed, often 256) — flag pushes that overflow.
- **Specialization constants** (`layout(constant_id = N)`) baked into SPIR-V at pipeline-create; changing them at runtime requires pipeline re-creation. Flag uses where a plain uniform would suffice.
- **`gl_Layer` / `gl_ViewportIndex`** require `multiViewport` / `geometryShader` / `shaderOutputLayer` features — verify the device actually enables them.
- **Subgroup operations** (`subgroupBallot`, `subgroupBroadcast`, `subgroupAdd`, etc.) require `VK_EXT_subgroup_size_control` and feature flags. Flag uses without a comment documenting the feature requirement.
- **`VK_KHR_shader_non_semantic_info`** is needed for `debugPrintfEXT` — remind the user to remove printf before shipping.
- **Varying component budget**: Vulkan guarantees at least 64 output components (16 vec4s) between stages; some drivers silently truncate beyond that. Flag fragment-shader inputs that push past 16 `location` slots.

---

### 6. Compute-Shader-Specific Review (`.comp`)

In addition to §4:

- Are `barrier()` calls symmetric across all threads (no barrier inside a divergent branch)?
- Is `shared` memory initialized before the first read across threads (needs a barrier)?
- Does the dispatch dimension computation on the CPU round up correctly vs the `local_size`? (That's a C++ concern, but flag any shader that assumes a fixed grid shape.)
- Are atomic operations (`atomicAdd`, etc.) on a type the image/buffer format actually supports?

---

### 7. Stage-Specific Quick Checks

#### Vertex (`.vert`)
- `gl_Position.w` divided correctly (normally handled by the pipeline, but verify if emitting clip-space manually).
- Attribute `layout(location = N)` matches the C++ `VkVertexInputAttributeDescription` — mismatches show as zero/garbage attributes, no validation error on most drivers.

#### Fragment (`.frag`)
- See §4b on `discard` and divergence.
- `gl_FragDepth` writes disable early-Z for the whole pipeline; flag any new `gl_FragDepth` assignment.
- Output `location`s match the render pass's color attachments; MRT writers must write all enabled attachments or the others get undefined values.

(Geometry, tessellation, mesh/task, and ray-tracing stages are not currently used in the repo. If one starts being used, see `references/shader-footguns.md` for stage-specific checks.)

---

### 8. File-Size and Complexity

- Shaders over ~500 lines: flag as `RECOMMEND` splitting into multiple stages via `#include`d helpers. The repo's convention is one shader per file with shared logic in `ShaderFunctions.h` or subdirectory `*Common.h`.
- Functions over ~100 lines inside a fragment shader: flag if a natural split exists (lighting term, material evaluation, tone mapping).

---

### 9. API Verification

For non-obvious GLSL/Vulkan calls — extension intrinsics, recent SPIR-V opcodes, GLSL features beyond `#version 460`, subgroup ops, ray-tracing intrinsics, image-format-specific atomics — verify against the official spec before accepting the call. This review normally runs inside a subagent, which does not spawn further subagents and should not pull large spec pages into its context: emit each needed check as an entry under `### API Verification Requests` in the output — the API/symbol, the spec URL, and exactly what to confirm plus which finding depends on it — and the caller dispatches Sonnet fetch subagents to resolve them. Use WebFetch directly only for a small targeted page (a single man-page-style entry), citing the URL or section in the review note.

- Khronos GLSL spec (4.60): https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html
- Vulkan 1.2 spec: https://registry.khronos.org/vulkan/specs/1.2-extensions/man/html/
- SPIR-V Extended Instructions for GLSL: https://registry.khronos.org/SPIR-V/specs/unified1/GLSL.std.450.html
- Khronos GLSL extensions: https://github.com/KhronosGroup/GLSL/tree/main/extensions

Skip verification for `texture()`/`texelFetch`-class staples, basic intrinsics (`mix`/`smoothstep`/`clamp`), and patterns already used at multiple shader sites. Training data lags Vulkan extension churn; an extension may have been promoted to core, deprecated, or had its semantics tightened.

If WebFetch turns up nothing authoritative, mark the finding:

> UNVERIFIED: I could not find official documentation for this pattern. This is based on training data and may be outdated. Verify before using in production.

---

## Output Format

Only include sections where issues were found. Omit empty sections entirely.

```
## GLSL Review Results

### Files Reviewed
- [list of modified shader files with paths]

### Broken Engine Rule Violations (hard)
- file:line - Violation and fix (inverse() ban, std140/std430, descriptor set, bindless nonuniformEXT, header sync)

### Correctness Issues
- file:line - NaN/Inf source, coord-system mix-up, wrong math, or interpolation-qualifier bug with fix

### Performance Issues
- file:line - Divergence, early-Z kill, hot-path inverse/pow/trig, dependent fetch, memory-access hazard

### Vulkan / API Issues
- file:line - Push-constant overflow, missing feature flag, subgroup-op hazard

### File Size Warnings
- file (N lines) - [RECOMMEND] split or extract into ShaderFunctions.h

### API Verification Requests
- <api/symbol> — <spec URL> — <what to confirm, and which finding depends on it>

### Recommendation
[PASS / NEEDS FIXES]
Brief summary.
```

If no issues in any category: "PASS — no issues found." plus the Files Reviewed list.

---

## See Also

- `references/shader-footguns.md` — exhaustive catalog with rationale per item, sourced from Khronos GLSL spec, NVIDIA/AMD best-practice guides, and Arm Mali docs.
- `Engine/Data/Shaders/AGENTS.md` — repo-level shader architecture (scalar layout, bindless, descriptor sets, NVIDIA bug).
- `Engine/Data/Shaders/ShaderFunctions.h` — shared utilities you should reach for before reimplementing.
- `/repo-code-review` — sibling skill for C++ changes.
