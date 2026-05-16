# Descriptor-Set Compute-Shader Sweep

## Context

The engine's shader-tree descriptor-set convention (per `Engine/Data/Shaders/CLAUDE.md`) is:

- **Set 0** — global (per-frame UBOs, bindless texture array, shared samplers; owned by `TextureDescriptors` and bound externally)
- **Set 1** — per-pipeline (SSBOs, combined image samplers, storage images)
- **Set 2** — per-material (model pipelines only)

Graphics shaders already comply: every `.vert` / `.frag` in the tree carries explicit `layout(set = N, binding = M, ...)` qualifiers and `CreateDescriptorSetLayouts` in `PipelineCreator.cpp` partitions them by reflected set index.

Compute shaders do not. Every `.comp` under `Engine/Data/Shaders/` declares bindings with `layout(binding = N, ...)` and no `set =` qualifier — they all default to set 0 regardless of role. A recent audit catalogued ~50 such violations across the Wind, Smoke, Shadow, and Lighting subsystems.

**This plan is blocked on the prerequisite engine plan `Documents/Plans/Engine/Architecture_MultiSetComputePipelines.md`.** That plan brings the compute pipeline-creation path into structural parity with graphics:

- `PipelineCreator::CreateComputePipeline` (`Engine/Source/Graphics/Objects/PipelineCreator.cpp:578-639`) is refactored to split bindings by reflected SPIR-V set index (mirroring `CreateDescriptorSetLayouts` at `PipelineCreator.cpp:107-225`), so set 0 bindings are routed to the external global set and set 1 bindings get their own per-pipeline layout.
- `TextureDescriptors::Create` (`Engine/Source/Graphics/Managers/TextureDescriptors.cpp:23-30`) widens `stageFlags` on the global set's UBOs / bindless array / shared samplers to include `VK_SHADER_STAGE_COMPUTE_BIT`.
- `Pipeline::Create` drops the `!(mInfo.flags & kCompute)` gate so compute pipelines also attach `mVkExternalDescriptorSetLayout`.
- `PipelineDescriptorWriter::Write` routes writes by reflected set index for compute too.
- A new `BindComputeDescriptorSets` helper in `Pipeline.cpp` replaces the five inline `vkCmdBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE, ...)` call sites in `Pipeline.cpp` (`RecordCompute`, `RecordComputeIndirect`) and `CommandBufferRecordGlobal.cpp` (`RecordWindSpreadPipeline`, `RecordSmokeSpreadPipeline`).

Once the prerequisite lands, this sweep is **pure GLSL** — every per-file edit below adds one explicit `set = N` qualifier per binding declaration. No C++ side changes. The compute shader-info already carries `pDescriptorSetIndices` populated by spirv-cross in the DataPacker; the engine path was just ignoring it.

Both `Documents/Plans/Graphics/ShaderReview/06_Lighting.md` (under `LightingBlurH.comp` / `LightingBlurV.comp`) and `Documents/Plans/Graphics/ShaderReview/10_Shadow.md` (implicitly across all shadow `.comp` files) already flag the descriptor-set issue in their notes. Those plans retain ownership of every other correctness / perf item they list (Gaussian sigma misnaming, `writeonly` qualifiers, `textureLod` migration, NaN guards, workgroup-size review, etc.). Only the descriptor-set portion lands here, so this sweep can be executed mechanically without entangling the correctness work.

## Design

Two-rule edit recipe applied to every `.comp` listed in `## Execution steps`:

1. **`set = 0`** for bindings that name a global resource: the `globalUniform` UBO and (where present) the `mainUniform` UBO. Their data is supplied externally by `TextureDescriptors`'s global set, per the prerequisite engine plan.
2. **`set = 1`** for everything else declared in the file: combined image samplers (`sampler2D`), storage images (`image2D`), and SSBOs (`buffer`). These are per-pipeline resources that `PipelineDescriptorWriter` will continue to write into `rPipeline.mVkDescriptorSets[iFramebuffer]`.

The edit is mechanical: each `layout (binding = N, ...)` declaration becomes `layout (set = M, binding = N, ...)`. No binding numbers change. No descriptor layout shape changes from the shader's point of view — the C++ side already knows the layout shape from the prerequisite plan's set-index split.

Shaders that use push constants only (no descriptor bindings) need no edit. Shaders that mix push constants with per-pipeline samplers/images still follow the rule: only the descriptor bindings carry `set =` qualifiers; the push-constant block does not.

## Execution steps

Line numbers were verified at plan-authoring time and may drift before execution — symbol identity (the binding-block opening `layout(...)` line) is the stable anchor. Refresh via `/next-plan` if line drift is suspected.

### Wind (3 files, ~15 binding declarations)

`Engine/Data/Shaders/Wind/WindSpreadOne.comp`:
- L10 `globalUniform` → `set = 0`
- L15 `windTextureSampler` → `set = 1`
- L16 `noiseTextureSampler` → `set = 1`
- L17 `outputImage` → `set = 1`
- L20 `activeTileBuffer` → `set = 1`
- L28 `occupancyBuffer` → `set = 1`

`Engine/Data/Shaders/Wind/WindSpreadTwo.comp`: identical shape to WindSpreadOne, same edits at same lines.

`Engine/Data/Shaders/Wind/WindOccupancyDilate.comp`:
- L8 `globalUniform` → `set = 0`
- L13 `occupancyBuffer` (readonly) → `set = 1`
- L19 `activeTileBuffer` → `set = 1`

### Smoke (4 files, ~23 binding declarations)

`Engine/Data/Shaders/Smoke/SmokeSpreadOne.comp`:
- L10 `globalUniform` → `set = 0`
- L15-L19 samplers + image (`textureSampler`, `noiseTextureSampler`, `windTextureSamplerOne`, `windTextureSamplerTwo`, `outputImage`) → `set = 1`
- L22 `activeTileBuffer` → `set = 1`
- L30 `occupancyBuffer` → `set = 1`

`Engine/Data/Shaders/Smoke/SmokeSpreadTwo.comp`:
- L10 `globalUniform` → `set = 0`
- L15-L20 samplers + image → `set = 1`
- L23 `activeTileBuffer` → `set = 1`
- L31 `occupancyBuffer` → `set = 1`

`Engine/Data/Shaders/Smoke/SmokeOccupancyDilate.comp`:
- L8 `globalUniform` → `set = 0`
- L13 `occupancyBuffer` → `set = 1`
- L19 `activeTileBuffer` → `set = 1`

`Engine/Data/Shaders/Smoke/SmokeOccupancyDilateRemap.comp`:
- L8 `globalUniform` → `set = 0`
- L13 `occupancyBuffer` → `set = 1`
- L19 `activeTileBuffer` → `set = 1`

### Shadow (5 files, ~15 binding declarations)

All five shadow compute shaders declare `globalUniform` at binding 0, a sampler at binding 1, and an output image at binding 2:

`Engine/Data/Shaders/Shadow/Shadow.comp`:
- L8 `globalUniform` → `set = 0`
- L13 `elevationTextureSampler` → `set = 1`
- L14 `shadowTexture` → `set = 1`

`Engine/Data/Shaders/Shadow/ShadowBlurH.comp`, `ShadowBlurV.comp`, `ObjectShadowsBlurH.comp`, `ObjectShadowsBlurV.comp`: same shape — `globalUniform` at L8 → set 0; sampler at L13 → set 1; output image at L14 → set 1.

### Lighting (2 files, 4 binding declarations)

`Engine/Data/Shaders/Lighting/LightingBlurH.comp` (no `globalUniform` — push constants only):
- L12 `sourceSampler` → `set = 1`
- L13 `blurIntermediateTexture` → `set = 1`

`Engine/Data/Shaders/Lighting/LightingBlurV.comp`: same shape — both bindings → `set = 1`.

Total: 14 files, ~50 binding declarations migrated.

## Critical files

- `Engine/Data/Shaders/Wind/WindSpreadOne.comp`, `WindSpreadTwo.comp`, `WindOccupancyDilate.comp` — Wind subsystem (3 files).
- `Engine/Data/Shaders/Smoke/SmokeSpreadOne.comp`, `SmokeSpreadTwo.comp`, `SmokeOccupancyDilate.comp`, `SmokeOccupancyDilateRemap.comp` — Smoke subsystem (4 files).
- `Engine/Data/Shaders/Shadow/Shadow.comp`, `ShadowBlurH.comp`, `ShadowBlurV.comp`, `ObjectShadowsBlurH.comp`, `ObjectShadowsBlurV.comp` — Shadow subsystem (5 files).
- `Engine/Data/Shaders/Lighting/LightingBlurH.comp`, `LightingBlurV.comp` — Lighting subsystem (2 files).
- `Documents/Plans/Engine/Architecture_MultiSetComputePipelines.md` — prerequisite plan; do not start this sweep until that plan has landed and validation-layer-clean compute dispatch is confirmed.

## Out of scope

- **Any changes to graphics (`.vert` / `.frag`) shaders.** Verified at plan-authoring time: every graphics shader in the tree already carries explicit `set = N` qualifiers and is compliant with the convention. This includes `Objects/HexShield.frag`, `Lighting/LightingSpread.frag`, `Water.frag`, `Terrain.frag`, `DebugTexture.frag`, `Ui/ProfileText.frag`, and all `.vert` files — all explicitly use the correct sets already. An earlier sweep mis-flagged these; reading the files confirmed compliance.
- **Any C++ side changes.** Pipeline-creation refactor, `TextureDescriptors` stage-flag widening, `PipelineDescriptorWriter` set-routing, and the new `BindComputeDescriptorSets` helper all belong to `Documents/Plans/Engine/Architecture_MultiSetComputePipelines.md`. This sweep assumes that plan has already landed; the diff here is pure GLSL.
- **Every non-descriptor-set correctness/perf item in `Documents/Plans/Graphics/ShaderReview/06_Lighting.md` and `10_Shadow.md`.** Those plans retain ownership of: `writeonly` qualifiers on output images; `textureLod(...)` migration for compute-stage sampling; NaN/Inf guards on `pow()` and Gaussian sigma divides; `fInvSigma = iRadius / fSigma` algebra mislabel; sigma-zero guards; pixel-center UV `+0.5` offset; weight-sum normalization in shadow blurs; workgroup-size review; hue-preserve dead-code in `LightCombine.comp`; `pPassCount`/`m` zero-guards in `LightCombine.comp`. Only the per-file `set =` qualifier additions land here.
- **`Documents/Plans/Graphics/ShaderReview/02_Water.md`.** Water is fragment-only; the descriptor-set audit there is about whether several `set = 1` samplers should move to `set = 0`. Different question, different shader stage; resolution stays in 02_Water.md.
- **`LightCombine.comp`.** This compute shader uses a bindless `sampler2D` array pattern that matches the global bindless infrastructure. Confirm intent with a comment in that file rather than migrating it; this sweep does not touch it.
- **Particle compute shaders (`ParticlesSpawn.comp`, `ParticlesUpdate.comp`, `LongParticlesUpdate.comp`).** Not in the original catalogue. A follow-up audit may extend the rule there; do not pre-emptively widen scope.
- **Push-constant declarations.** `layout(push_constant) uniform ...` blocks carry no `set =` qualifier in any shader.
- **Order.md edit.** Reserved for the parent agent — this plan only proposes the row.

## Acceptance criteria

- Vulkan validation layer reports zero errors at client launch with Wind, Smoke, Shadow, and Lighting blur dispatches all exercised (camera move + explosion + lighting parameter slider).
- Visual diff across all 14 shaders' subsystems is zero: side-by-side launch of pre-sweep and post-sweep builds on a captured map shows identical Wind velocity field, identical Smoke density evolution, identical terrain and object shadow falloff, identical lighting blur intermediates.
- `Grep` for `layout\s*\(\s*binding` across `Engine/Data/Shaders/**/*.comp` returns zero matches that lack a `set =` prefix (every binding now carries an explicit set qualifier).
- DataPacker recompile of all `.comp` files completes without SPIR-V reflection errors; `pDescriptorSetIndices` populated by spirv-cross now contains a mix of `0` and `1` per binding (currently all-zero).

## Notes

- The prerequisite engine plan leaves all-set-0 compute shaders working unchanged (empty set-1 layout, single bind site with empty set 1). That means the prerequisite can ship before this sweep. Validation errors that the sweep is fixing only appear once the prerequisite is in: this plan's value lands when the global set's compute stage flags are widened and an empty set-1 layout is being created per pipeline.
- Symbol-anchored line numbers: each binding's `uniform`/`buffer`/`image2D` block name is the stable anchor for `/next-plan`'s refresh pass — the leading `layout(...)` line is the only line that changes per file.
- The edit can be applied in one mechanical pass with a regex-aware editor (`layout\s*\(\s*binding\s*=\s*(\d+)` → `layout (set = M, binding = $1`), with the rule "M = 0 if and only if the binding's block name is `globalUniform` or `mainUniform`; M = 1 otherwise" applied per match.
- This sweep is the second half of the descriptor-set convention story. The first half — engine plumbing — is `Architecture_MultiSetComputePipelines.md`. Together they bring the engine's compute path into structural parity with its graphics path and codify the set 0 / set 1 / set 2 layering documented at `Engine/Data/Shaders/CLAUDE.md`.
