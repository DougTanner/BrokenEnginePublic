---
name: glsl-review
description: >-
  Review changed Vulkan GLSL shaders and shader-facing shared headers for
  correctness, performance, Broken Engine layout and binding contracts, and
  missing rationale. Use after changing .vert, .frag, .comp, other GLSL stages,
  .glsl includes, or dual-language headers under Engine/Data/Shaders or
  Projects/*/Data/Shaders, and when the user asks to review, audit, or verify
  shader code. Covers NaN/Inf hazards, synchronization, derivatives, scalar
  layout, CPU/GLSL dependency propagation, descriptor indexing, and the
  repository inverse() ban.
allowed-tools: [Read, Grep, Glob, Bash, PowerShell]
paths: ["**/*.vert", "**/*.frag", "**/*.comp", "**/*.geom", "**/*.tesc", "**/*.tese", "**/*.mesh", "**/*.task", "**/*.rgen", "**/*.rmiss", "**/*.rchit", "**/*.rahit", "**/*.rint", "**/*.rcall", "**/*.glsl", "**/Data/Shaders/**/*.h"]
---

# GLSL Review

Review findings only; never edit. Cover correctness and performance, not style. Treat source-adjacent rationale as required only when a non-obvious mathematical, numerical, coordinate, ordering, layout, or hardware assumption carries correctness or measured performance.

Read [the footgun reference](references/shader-footguns.md) when a changed region touches synchronization, subgroup operations, implicit derivatives, shared CPU/GLSL layout, clip coordinates, numerically guarded math, or a performance heuristic.

## Workflow

1. Derive the exact changed shader files and regions from the implementation handoff, conversation, or fixed-baseline diff. Include transitive shader headers and any dual-language header under `Engine/Data/Shaders/` or `Projects/*/Data/Shaders/`.
2. Read the applicable shader `AGENTS.md`, each changed file, its nearby producers/consumers, and the relevant whole function. Search `ShaderFunctions.h` and family `*Common.h` files before recommending new helper logic.
3. Trace every changed shader-facing header in both directions:
   - shader entry points that transitively `#include` it;
   - C++ consumers, including the project `ShaderLayouts.h` wrapper and PCH inclusion path;
   - DataPacker dependency capture from preprocessing (`-MD`/`-MF`) through dependency fingerprinting.
4. Review changed dual-language declarations against the actual block qualifier and CPU representation. Compare field order, scalar widths, array strides, offsets, descriptor constants, writes, and binding roles. Do not infer layout from a generic `vec3` rule when `layout(scalar)` applies.
5. Apply the checks below. Report only reachable failures supported by the changed code and repository evidence. Do not turn a generic checklist item into a finding.
6. Emit an atomic external-claim packet for every finding that depends on a non-obvious GLSL, Vulkan, extension, device, or compiler claim. Do not browse directly. Keep locally provable repository-contract findings separate.
7. Return the report. A shader-facing shared header has both C++ and GLSL surfaces, so explicitly require `/repo-code-review` as the sibling domain review when such a header changed; this review does not replace it.

## Broken Engine Contracts

- Flag every executable matrix `inverse(...)` call with a `mat3` or `mat4` operand/result. Ignore comments and string literals. The repository requires CPU-precomputed inverse transforms; inspect the current shared layout and upload path rather than relying on a stale line number.
- Require the repository's scalar-layout contract for shared UBO/SSBO data. Uniform blocks inherit the global scalar default; storage blocks must acquire scalar layout explicitly by block or shader default. Flag introduced `std140`/`std430`, unqualified storage blocks, and CPU/GLSL size or order mismatches.
- Enforce descriptor roles: set 0 global, set 1 per-pipeline, set 2 per-material. Verify shared binding constants against C++ layout creation and writes.
- Require `nonuniformEXT(index)` when a descriptor-array index may vary between invocations. Prove compile-time or dynamically uniform indices before exempting them.
- Treat changes to shader-facing shared headers as both C++ and GLSL changes. Verify that every affected shader entry point reaches the header through the include graph so DataPacker records it in that shader's dependency file.

## Correctness Checks

### Values and math

- Trace possible domains for division, `sqrt`, `normalize`, `pow`, `asin`/`acos`, `log`/`log2`, and `atan(y, x)`. Require a guard only when the bad domain is reachable.
- Preserve denominator sign when negative values are valid. `max(x, epsilon)` is correct only when `x` is proven nonnegative; otherwise use a sign-preserving clamp such as `x >= 0.0 ? max(x, epsilon) : min(x, -epsilon)`.
- Check normal reconstruction, color-space and alpha conventions, matrix order, coordinate-space conversions, shadow-bias direction, BRDF weighting, and renormalization after interpolation.
- Treat Kahan summation as an accuracy technique, not a determinism guarantee. Parallel or reordered sums still require a fixed reduction order or another proven deterministic representation.
- Require the same expression plus appropriate `invariant` qualification only when separate pipelines must produce invariant outputs; do not claim a qualifier alone guarantees arbitrary cross-program bit identity without verification.

### Stages, interpolation, and sampling

- Require `flat` for integer varyings and matching interface types, locations, and interpolation qualifiers between producer and consumer stages.
- Qualify derivative findings by stage. `dFdx`, `dFdy`, `fwidth`, and fragment implicit-LOD behavior depend on fragment-stage derivative rules unless an applicable extension establishes otherwise. Do not apply the fragment divergence rule indiscriminately to vertex or compute sampling.
- Distinguish `texture` from `texelFetch`: `texelFetch` uses integer texel coordinates and, when its overload requires one, an explicit LOD or sample; it performs no filtering and uses no implicit derivatives. Do not say it ignores sampler objects.
- Treat vertex `gl_Position` as homogeneous clip coordinates. Perspective division by `w` is a fixed-function operation; flag manual division or invalid/non-finite `w` only when the transform and intended convention make it wrong.
- Verify Vulkan viewport, depth, Y, and framebuffer-origin assumptions against the actual pipeline state before reporting a coordinate mismatch.

### Compute synchronization and storage

- Separate execution convergence, memory visibility/order, access qualification, and race prevention. `barrier()`, `memoryBarrier*`, `coherent`, and atomics are not interchangeable.
- Require workgroup barriers to be reached in uniform control flow. Account for the shared-memory synchronization defined for `barrier()` itself, and require the applicable additional memory operation for other storage classes rather than generalizing shared-memory behavior to buffers or images.
- Do not accept `coherent` plus a barrier as prevention for two invocations concurrently writing the same non-atomic location. Require exclusive ownership, a proven partition, or a supported atomic operation.
- Do not claim an in-shader workgroup barrier synchronizes separate workgroups. Cross-workgroup communication requires a design with an applicable dispatch/pipeline synchronization boundary or another proven mechanism.
- Check atomic type/format support and distinguish atomicity of one location from ordering or visibility of surrounding non-atomic data.

## Performance Review

- Require evidence before asserting device-, driver-, or compiler-specific cost. Accept a capture, target-device limit/property, compiler output, SPIR-V/disassembly, or a documented repository production constraint. Otherwise request measurement or emit external verification; do not report folklore as a defect.
- Inspect hot-path `pow`, transcendental functions, normalization, dependent reads, loop bounds, SSBO/image access, workgroup dimensions, bank layout, divergence, `discard`, and depth writes only where the changed path is demonstrably hot or contract-significant.
- For a positive integer exponent, compare semantics and generated code. `x * x * x` contains two multiplications, not one; do not assume how `pow` lowers without compiler evidence.
- Do not impose universal subgroup width, workgroup-size, bank-count, FP16-throughput, occupancy, or early-Z cost thresholds. Establish the target device and evidence first.

## Vulkan and Extension Checks

- Baseline subgroup support is described by `VkPhysicalDeviceSubgroupProperties`: verify supported stages and operations. Do not require `VK_EXT_subgroup_size_control` merely to use subgroup operations; require it only when code requests or depends on subgroup-size control/full-subgroup behavior, then verify its features and limits.
- Verify push-constant size/stage ranges, storage image formats, atomics, descriptor indexing, stage-specific built-ins, and optional shader extensions against the repository's Vulkan 1.2 target and enabled device features.
- Treat debug-only extensions and printf paths according to their actual compile guards and shipping configuration.

## External Claim Packets

Emit one proposition per packet so `/verify-external-claims` can return one verdict without adjudicating the code finding:

```markdown
### External Claim Verification Request
- API/symbol/rule: <one rule>
- Proposition: <exact statement that must be true or false>
- Applicability: Vulkan 1.2, shader stage, enabled extension/feature, format, and relevant local configuration
- Candidate official source: <direct official URL and section/anchor>
- Dependent finding: <file:line finding and why its severity depends on this proposition>
```

Use only the official source set in the reference. If the proposition cannot be made atomic, split it. Keep the finding pending until the caller obtains `VERIFIED`, `REFUTED`, or `UNRESOLVED` evidence.

## Output

Order findings by severity and omit empty sections:

```markdown
## GLSL Review Results

### Findings
- P1 `path:line` — failure, reachable evidence, and smallest correction

### External Claim Verification Requests
<atomic packets>

### Files Reviewed
- `path`

### Recommendation
PASS | NEEDS FIXES

Status: PASS | NEEDS_ACTION | BLOCKED
Files changed: none
Functions/regions touched: none
Decisive checks: <reads, searches, traces, and external-claim dispositions>
Build required: none
Residuals:
- <pending verification, pre-existing issue, incomplete review item, or none>
```

If no issue is found, return `PASS — no issues found`, list the files reviewed, and include the unchanged footer.
