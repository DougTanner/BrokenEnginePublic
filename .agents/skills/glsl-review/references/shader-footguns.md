# Shader Review Reference

Use this reference to reason about changed shader code. It records review distinctions, not unconditional findings. External behavior remains pending until `/verify-external-claims` resolves an atomic proposition.

## Table of Contents

- [Official source set](#official-source-set)
- [Numerical domains](#numerical-domains)
- [Coordinates, interpolation, and sampling](#coordinates-interpolation-and-sampling)
- [Synchronization and data races](#synchronization-and-data-races)
- [Subgroups](#subgroups)
- [CPU/GLSL layout and dependency propagation](#cpuglsl-layout-and-dependency-propagation)
- [Performance evidence](#performance-evidence)
- [Algorithm checks](#algorithm-checks)
- [Optional stages and features](#optional-stages-and-features)

## Official source set

Use only these official sources in external-claim requests. Give the exact applicable section or anchor in the request when known.

- GLSL 4.60 specification: `https://registry.khronos.org/OpenGL/specs/gl/GLSLangSpec.4.60.html`
- Vulkan 1.2 data races: `https://registry.khronos.org/vulkan/specs/1.2-khr-extensions/html/vkspec.html#memory-model-data-races`
- `VkPhysicalDeviceSubgroupProperties`: `https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceSubgroupProperties.html`
- `VK_EXT_subgroup_size_control`: `https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_subgroup_size_control.html`
- Vulkan coordinate transformations: `https://registry.khronos.org/vulkan/specs/1.2-khr-extensions/html/vkspec.html#vertexpostproc-coord`

Do not cite blogs, tutorials, vendor performance generalizations, stale repository line numbers, or nonexistent Vulkan man-page roots. A repository production constraint may support a local finding, but do not present it as a portable API fact.

## Numerical domains

| Operation | Reachable failure | Review-safe direction |
| --- | --- | --- |
| `sqrt(x)` | `x < 0` | Prove nonnegative or clamp to zero. |
| `a / x` | zero or near-zero `x` | Guard magnitude; preserve sign if negative values are valid. |
| `normalize(v)` | zero length | Prove nonzero or define an explicit fallback. |
| `pow(x, y)` | negative base with unsuitable exponent | Prove domain or reformulate without changing intended negative-input behavior. |
| `asin(x)`, `acos(x)` | input outside `[-1, 1]` | Clamp when rounding can escape the domain. |
| `log(x)`, `log2(x)` | nonpositive input | Prove positive or clamp to a justified minimum. |
| `atan(y, x)` | both arguments zero | Define the intended fallback if reachable. |

`max(x, epsilon)` destroys the sign of a negative denominator. When both signs are valid, use a sign-preserving form such as:

```glsl
float safeX = x >= 0.0 ? max(x, epsilon) : min(x, -epsilon);
```

Normal-map Z reconstruction commonly needs `sqrt(max(0.0, 1.0 - dot(xy, xy)))` because filtering or compression can make the squared XY length exceed one.

Kahan summation reduces sequential rounding error. It does not make a result independent of term order, thread scheduling, or atomic update order. Deterministic accumulation needs a fixed reduction order, exact/fixed-point representation, or another proven scheme.

Require the same expression plus appropriate `invariant` qualification only when separate pipelines must produce invariant outputs. Do not claim a qualifier alone guarantees arbitrary cross-program bit identity without verification.

## Coordinates, interpolation, and sampling

`gl_Position` is a homogeneous clip-space value. The fixed-function coordinate transform performs perspective division using `w`; a vertex shader normally must not pre-divide and then leave a non-unit `w`. Review the complete transform, clipping intent, and pipeline viewport state. Use the Vulkan coordinate-transform source for any non-obvious API proposition.

Check stage interfaces as pairs. Integer varyings require `flat`; producer and consumer types, locations, and qualifiers must agree. Interpolated directions and normals generally need renormalization before length-dependent use.

Derivative reasoning is stage-qualified:

- In fragment shaders, implicit-LOD texture sampling and derivative functions interact with non-uniform control flow.
- Do not transplant that rule to vertex, compute, or another stage without checking the GLSL rule or an enabled extension for that stage.
- Emit a GLSL 4.60 verification request when a finding depends on derivative availability or definedness.

`texelFetch` uses integer texel coordinates and, when required by its overload, an explicit LOD or sample parameter. It does not filter and does not compute implicit derivatives. It still operates through a sampler-typed GLSL object; do not describe sampler objects as ignored.

Verify sRGB conversion, premultiplied versus straight alpha, depth range, framebuffer origin, Y orientation, and shadow comparison direction against image formats and pipeline state rather than a generic OpenGL-to-Vulkan checklist.

## Synchronization and data races

Keep four questions separate:

| Mechanism | Question it addresses | What it does not prove |
| --- | --- | --- |
| `barrier()` | Did participating workgroup invocations reach an execution rendezvous in valid control flow, including its defined shared-memory synchronization? | Visibility for unrelated storage classes, cross-workgroup synchronization, or exclusive writes. |
| `memoryBarrierShared`, `memoryBarrierBuffer`, `memoryBarrierImage`, related operations | Are relevant memory operations ordered/made available and visible under the applicable GLSL/Vulkan rules? | Execution convergence or race-free conflicting writes by themselves. |
| `coherent` | Do accesses use the coherent visibility semantics applicable to that storage? | Mutual exclusion or atomic read-modify-write. |
| Atomic operation | Is the operation on its addressed location indivisible under the supported type/format rules? | Ordering of unrelated non-atomic payload or whole-algorithm determinism. |

For workgroup communication, inspect write ownership, storage class, memory operation, and uniform reachability of the execution barrier. Do not prescribe `memoryBarrierShared()` by rote; verify whether `barrier()` supplies the needed shared-memory synchronization for the exact access pattern, and require the applicable memory operation for buffer or image communication. A barrier nested in control flow that not all required invocations reach is suspect.

Two invocations writing the same non-atomic location can race even when the variable is coherent and barriers surround the phase. Resolve through exclusive ownership, a proven partition, or supported atomics. When atomics publish surrounding payload, separately verify the required memory ordering and visibility.

An in-shader workgroup barrier does not provide a rendezvous among independent workgroups. Algorithms requiring global phase completion need an applicable multi-dispatch/pipeline synchronization design or another mechanism proven for the target. Emit separate GLSL/Vulkan data-race packets for non-obvious propositions; do not bundle execution and visibility into one claim.

## Subgroups

For Vulkan 1.2, first inspect `VkPhysicalDeviceSubgroupProperties` for the subgroup size, supported shader stages, and supported operations needed by the shader. Subgroup operations do not categorically require `VK_EXT_subgroup_size_control`.

Inspect `VK_EXT_subgroup_size_control` only when the implementation requests, specializes for, or otherwise depends on a controllable subgroup size or full-subgroup behavior. Then verify extension/feature enablement, required-subgroup-size stages, and relevant limits. Avoid assuming a fixed width from a vendor name.

Emit two requests when both baseline operation support and size-control behavior matter; they are independent propositions with different official sources.

## CPU/GLSL layout and dependency propagation

The repository's dual-language path is:

1. `Projects/BrokenEngineSandbox/Data/Shaders/ShaderLayouts.h` includes `Engine/Data/Shaders/ShaderLayoutsBase.h`.
2. The project PCH includes the wrapper for C++ consumers; `BT_ENGINE` selects C++ representations.
3. Shader entry points include shared shader headers; the GLSL branch enables the scalar-layout extension and establishes the uniform default.
4. DataPacker preprocesses each shader with `glslc -MD -MF`, parses the generated dependency file, fingerprints dependencies below the engine/project input roots, and dirties the shader when a recorded dependency changes.

Therefore review a shared-header edit as a graph change, not merely a declaration diff:

- trace every top-level shader that transitively includes it;
- confirm the changed declaration is present on the intended C++ and GLSL branches;
- compare exact CPU and GLSL scalar widths, field order, arrays, and block qualifiers;
- trace C++ descriptor layouts, writes, upload sizes, and static assertions;
- ensure each affected shader's include path lets preprocessing record the header as its dependency.

Under the repository contract, uniform blocks inherit scalar layout and storage blocks acquire scalar layout explicitly. Plain scalar arrays in scalar-layout blocks retain scalar stride. Do not apply `std140` padding rules or the usual non-scalar `vec3` alignment rule to a scalar-layout block. Conversely, do not assume a declaration is scalar merely because a shared struct exists; inspect the containing block/default.

A shader-facing `.h` change requires both `/glsl-review` and `/repo-code-review`, because neither domain review covers the other language surface completely.

## Performance evidence

Report performance defects only with a concrete signal: profiler capture, target-device property/limit, measured scene behavior, compiler diagnostics, generated SPIR-V/disassembly, or a documented repository production constraint. Without one, request evidence or frame an external-claim request; do not prescribe a universal threshold.

Apply this discipline to:

- workgroup dimensions, subgroup width, shared-memory banks, occupancy, and register pressure;
- FP16 throughput and precision tradeoffs;
- divergence, discard/demote, depth-write, and early-test behavior;
- dynamic-loop unrolling, dependent reads, sampler latency, and compiler lowering;
- claims that a polynomial, LUT, or manual expansion is faster on supported devices.

For `pow(x, 3.0)`, `x * x * x` visibly contains two multiplications. Check the input domain and generated code before calling the replacement equivalent or faster; compiler lowering is not established by source spelling alone. Apply the same evidence rule to integer exponents and transcendental approximations.

The Broken Engine `inverse()` prohibition is a repository production constraint, so executable matrix `inverse()` is a hard local finding even without a portable compiler claim. Verify the current CPU-precomputed alternative and its layout/upload path.

CPU-precomputable expressions are the general form of that rule and need no external evidence — invocation-invariance is proven from the source alone. Before reporting one:

- Trace every operand to its origin. Only uniforms, push constants, spec/compile-time constants, and pure functions of those qualify. A `gl_*` builtin, vertex input, varying, shared/storage read, image load, or `texture` result anywhere in the chain disqualifies the whole expression — but a maximal uniform-only sub-expression inside it can still hoist.
- Check whether the CPU already uploads the value in final form before proposing a new field: `GlobalUniforms.cpp` stores `f4SunMoonNormal` pre-normalized, and `ShaderLayoutsBase.h` carries precomputed reciprocals (`fSmokeObjectHeightInv`, `fWaterColorHeightInv`). Re-deriving such a value in-shader is a finding; the fix is deletion, not another field.
- Name the concrete CPU-side home in the finding: the target field (existing, or new in `GlobalLayout`/`MainLayout` under the scalar-layout contract) and the `Engine/Source/Graphics/Render/*Uniforms.cpp` populator that owns it.
- Weigh triviality: a single uniform-only operation the compiler folds anyway (a negation, one multiply of two scalars used once) may not justify a layout change; a normalize, divide, transcendental, or multi-operation chain in a per-fragment or per-invocation path does.

## Algorithm checks

- `mix` of unit directions does not generally preserve length; renormalize when later math assumes unit length.
- `reflect(I, N)` preserves the length of `I` only under the function's required normal assumptions. Remove or require normalization only after proving inputs at the call site.
- Verify matrix multiply order and every coordinate-space transition from definitions, not from variable names.
- Check half-vector construction, Fresnel domain, diffuse/specular weighting, tangent basis, normal reconstruction, shadow kernel/bias, and tone-map/gamma ordering against the shader family's documented model.
- Require a source-adjacent invariant for non-obvious grid indexing, phase ordering, approximations, fallback values, or correctness-critical constants. Do not demand narration of standard operations.

## Optional stages and features

For geometry, tessellation, mesh/task, and ray-tracing shaders, trace the matching C++ pipeline topology, enabled extension/features, limits, stage interfaces, shader binding table, and payload/attribute types. Do not assume an extension is enabled because the compiler accepts syntax. Emit one verification request per exact feature, limit, or stage-rule proposition.
