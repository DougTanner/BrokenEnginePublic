---
name: glsl-review
description: >-
  Review changed Vulkan GLSL shaders and shader-facing shared headers for
  correctness, performance, and Broken Engine layout and binding contracts. Use
  after changing GLSL shader sources, includes, or dual-language headers under
  Data/Shaders, and when the user asks to review, audit, or verify shader code.
allowed-tools: [Read, Grep, Glob, Bash, PowerShell]
paths: ["**/*.vert", "**/*.frag", "**/*.comp", "**/*.geom", "**/*.tesc", "**/*.tese", "**/*.mesh", "**/*.task", "**/*.rgen", "**/*.rmiss", "**/*.rchit", "**/*.rahit", "**/*.rint", "**/*.rcall", "**/*.glsl", "**/Data/Shaders/**/*.h"]
---

# GLSL Review

Run one fresh `reviewer` pass. Review findings only; never edit or delegate.
Cover correctness and performance, not style. Treat source-adjacent rationale
as required only when a non-obvious mathematical, numerical, coordinate,
ordering, layout, or hardware assumption carries correctness or measured
performance.

Read the footgun reference (`references/shader-footguns.md`) on every review. It holds the correctness checks this review applies: numerical domains, coordinates, interpolation and sampling, synchronization and data races, subgroup operations, shared CPU/GLSL layout, performance evidence, algorithm checks, and optional stages.

## Workflow

1. Take the changed shader files and regions from the read-only inventory: `pwsh -NoProfile -File .agents/scripts/Get-SessionChangeInventory.ps1 -RepositoryRoot <absolute repository toplevel> -Baseline <full 40-character SHA> -Regions` (add `-Head <commit>` for a committed head; in Claude Code's Git Bash terminal convert the script path and root with `cygpath -w` exactly as `../cleanup-worktrees/SKILL.md` shows). It writes no file and prints one `broken-engine-session-change-inventory/v1` object: every `entries` row whose `class` is `glsl` or `dual-language-header` is in scope, and those class rules are the only statement of which `.h` files are shader sources — never restate or reconstruct that classification inline. The `regions` rows give the changed ranges. Only `status` `pass` (exit 0) is usable; `blocked` (exit 2) or `error` (exit 1) means the changed-file list is unavailable — report that outcome instead of reconstructing the list inline. The entries list is capped at 500 rows and the regions table at 400, and shader selection depends on both, so read `truncation.entries`, `truncation.regions`, and `truncated`: an emitted count below the full count in either one makes the selection incomplete, which blocks the review exactly as a non-pass status does. An untracked shader file appears only when the caller supplies it with `-IncludeUntracked <comma-separated paths>`, and `counts.unlistedUntracked` reports how many untracked files the run did not list. Include the transitive shader headers those files reach.
2. Read the applicable shader `AGENTS.md`, each changed file, its nearby producers/consumers, and the relevant whole function. Search `ShaderFunctions.h` and family `*Common.h` files before recommending new helper logic.
3. Trace every changed shader-facing header in both directions:
   - shader entry points that transitively `#include` it;
   - C++ consumers, including the project `ShaderLayouts.h` wrapper and PCH inclusion path;
   - DataPacker dependency capture from preprocessing (`-MD`/`-MF`) through dependency fingerprinting.
4. Review changed dual-language declarations against the actual block qualifier and CPU representation. Compare field order, scalar widths, array strides, offsets, descriptor constants, writes, and binding roles. Do not infer layout from a generic `vec3` rule when `layout(scalar)` applies.
5. Apply the correctness checks in the footgun reference. Report only reachable failures supported by the changed code and repository evidence. Do not turn a generic checklist item into a finding.
6. Emit an external-claim request covering one single checkable statement for every finding that depends on a non-obvious GLSL, Vulkan, extension, device, or compiler claim. Do not browse directly. Keep locally provable repository-contract findings separate.
7. Return the report. A shader-facing shared header has both C++ and GLSL surfaces, so explicitly require `/repo-code-review` as the sibling domain review when such a header changed; this review does not replace it.
8. Report a changed shader over ~5,000 `bt-token-v1` (measure every changed shader in one batched run: `pwsh -NoProfile -Command "& '<absolute path to Measure-Tokens.ps1>' -Path 'a','b','c' -Json"` — use `-Command`, not `-File`, because under `-File` the comma-separated list binds as one filename and the run fails; the single-path `-File ... -Path <path>` form still works for one file) as a size observation in `Residuals`; splitting it via `ShaderFunctions.h`/`*Common.h` is follow-up work, not part of this findings-only review.

## Broken Engine Contracts

- Flag every executable matrix `inverse(...)` call with a `mat3` or `mat4` operand/result. Ignore comments and string literals. The repository requires CPU-precomputed inverse transforms; inspect the current shared layout and upload path rather than relying on a stale line number.
- Require the repository's scalar-layout contract for shared UBO/SSBO data. Uniform blocks inherit the global scalar default; storage blocks must acquire scalar layout explicitly by block or shader default. Flag introduced `std140`/`std430`, unqualified storage blocks, and CPU/GLSL size or order mismatches.
- Enforce descriptor roles: set 0 global, set 1 per-pipeline, set 2 per-material. Verify shared binding constants against C++ layout creation and writes.
- Require `nonuniformEXT(index)` when a descriptor-array index may vary between invocations. Prove compile-time or dynamically uniform indices before exempting them.
- Treat changes to shader-facing shared headers as both C++ and GLSL changes. Verify that every affected shader entry point reaches the header through the include graph so DataPacker records it in that shader's dependency file.

## Performance Review

- Require evidence before asserting device-, driver-, or compiler-specific cost. Accept a capture, target-device limit/property, compiler output, SPIR-V/disassembly, or a documented repository production constraint. Otherwise request measurement or emit external verification; do not report folklore as a defect.
- Inspect hot-path `pow`, transcendental functions, normalization, dependent reads, loop bounds, SSBO/image access, workgroup dimensions, bank layout, divergence, `discard`, and depth writes only where the changed path is demonstrably hot or contract-significant.
- Flag an expression with at least one uniform or push-constant operand whose operands are all uniforms, push constants, spec/compile-time constants, or values derived only from those: every invocation computes the identical result, so it belongs on the CPU — precompute it into an existing field or a new uploaded field (reciprocals follow the existing `*Inv` naming; the `inverse()` ban and pre-normalized `f4SunMoonNormal` are the established precedents). A uniform-only sub-expression inside a varying expression qualifies too (`uniformA * uniformB * varying` hoists the uniform product). This is locally provable from the source; it needs no device or compiler evidence. Also flag the redundant twin: re-deriving on the GPU a value the CPU already uploads in final form (e.g. `normalize()` of an already-normalized uniform). Weigh triviality per the footgun reference before proposing a layout change.
- For a positive integer exponent, compare semantics and generated code. `x * x * x` contains two multiplications, not one; do not assume how `pow` lowers without compiler evidence.
- Do not impose universal subgroup width, workgroup-size, bank-count, FP16-throughput, occupancy, or early-Z cost thresholds. Establish the target device and evidence first.

## Vulkan and Extension Checks

- Baseline subgroup support is described by `VkPhysicalDeviceSubgroupProperties`: verify supported stages and operations. Do not require `VK_EXT_subgroup_size_control` merely to use subgroup operations; require it only when code requests or depends on subgroup-size control/full-subgroup behavior, then verify its features and limits.
- Verify push-constant size/stage ranges, storage image formats, atomics, descriptor indexing, stage-specific built-ins, and optional shader extensions against the repository's Vulkan 1.2 target and enabled device features.
- Treat debug-only extensions and printf paths according to their actual compile guards and shipping configuration.

## External Claim Requests

Emit one single-claim request per `/verify-external-claims` (`../verify-external-claims/SKILL.md`, `## External Claim Requests`); a pending verdict makes the review `NEEDS_ACTION` and keeps the dependent finding unconfirmed.

Use only the official source set in the reference.

## Output

Order findings by severity and omit empty sections:

```markdown
## GLSL Review Results

### Findings
- P1 `path:line` — failure, reachable evidence, and smallest correction

### External Claim Verification Requests
<single checkable requests>

### Files Reviewed
- `path`

### Recommendation
PASS | NEEDS FIXES

Functions/regions touched: none
```

Follow that extension field with the shared handoff lines (`../../references/subagent-reporting.md`, `## Handoffs`); this findings-only review never changes a file and never requires a build, and pending verification, a pre-existing issue, or an incomplete review item belongs in `Residuals`.

If no issue is found, return `PASS — no issues found`, list the files reviewed, and include the unchanged footer.
