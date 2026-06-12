# Architecture: Descriptor Set-Index Resolution Dedup

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Objects` (non-recursive). The logic that
resolves which descriptor set a binding belongs to — walking both shaders' reflected
`pDescriptorSetIndices` — is re-implemented four times across the two pipeline helper TUs. A change to the
set-resolution rule (e.g. a third set, or a new reflection field) must be made in four places that agree
today only by parallel evolution; this quadruplication is the real fragmentation in the
Pipeline/PipelineCreator/PipelineDescriptorWriter split.

## Design

### One shared resolver for the four copies
- Extract a single helper (e.g. static `Pipeline::ResolveBindingSetIndex(const PipelineInfo&, binding)` in
  `Pipeline.h`, or a free function in a small internal header next to `PipelineCreator.h` — grill the
  home) and route all four sites through it:
  - `PipelineCreator.cpp:168-190` — graphics descriptor-set-layout partition in `CreateDescriptorSetLayouts`
  - `PipelineCreator.cpp:654-668` — the compute-layout copy in `CreateComputePipeline`
  - `PipelineDescriptorWriter.cpp:15-37` — `BindingIsInSet0`
  - `PipelineDescriptorWriter.cpp:217-250` — `RouteWritesBySet`
  Behavior-identical: same reflected inputs, same set conclusions, byte-identical descriptor writes. [~45m]

## Critical files
- `Engine/Source/Graphics/Objects/Pipeline.h` (or a new internal header + vcxproj filter entry)
- `Engine/Source/Graphics/Objects/PipelineCreator.cpp`
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp`

## Out of scope
- The `Write`/`WriteModelDescriptor` decomposition — `Graphics/Refactor_WriteDescriptorDecomposition.md`
  (touches the same functions; co-schedule).
- The `CreateGraphicsPipeline` staged decomposition — `Graphics/Refactor_PipelineCreatorDecomposition.md`
  (same file; co-schedule).
- Any change to set-assignment semantics, binding numbers, or the `UINT32_MAX` bindless sentinel handling.

## Acceptance criteria
- Exactly one implementation of the set-index walk remains; all four former sites call it; client builds
  clean and renders identically (cold-path creation code, compile-checked + boot smoke).

## Notes
- Client-only pipeline-creation path; no determinism/CRC, `kiVersion`, replay, or network exposure.
- One grill decision: helper home (`Pipeline` static vs new internal header — the latter needs vcxproj
  filter wiring).
- Co-schedule with the two Refactor decomposition plans (same files; they move the lines this plan cites).

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source; all four sites read in full.
- The shared kernel is real, not superficial: each site resolves a binding's set index by indexing
  `pDescriptorBindings[uiBinding]` (binding number as array index), gating on `descriptorCount > 0` and the
  shader's reflected `iDescriptorSetLayoutBindings` bound, then reading `pDescriptorSetIndices[uiBinding]`,
  defaulting to set 0:
  - `PipelineCreator.cpp:168-190` — vertex-then-fragment walk (graphics layout partition);
  - `PipelineCreator.cpp:654-668` (loop body `:656-668`) — single-shader compute copy;
  - `PipelineDescriptorWriter.cpp:15-37` (`BindingIsInSet0`) — vertex-then-fragment with a `kCompute` guard
    skipping the second shader, plus an outer `mVkExternalDescriptorSetLayout == VK_NULL_HANDLE → false`
    early-out that stays at the call-site/wrapper level;
  - `PipelineDescriptorWriter.cpp:217-250` (`RouteWritesBySet`) — same walk with `pSecondShader = nullptr`
    for compute.
  A single `ResolveBindingSetIndex(const PipelineInfo&, uint32_t uiBinding)` (compute-ness from
  `PipelineInfo::flags & kCompute`, shaders from `ppShaders[0/1]`) covers all four; `BindingIsInSet0`
  becomes a two-line wrapper keeping its external-layout early-out.
- Behavior-identical claim holds: all four feed from the same reflected inputs
  (`Shader::mInfo.pDescriptorBindings` / `pDescriptorSetIndices`), so unifying cannot change descriptor
  writes or layouts.
- The `UINT32_MAX` bindless sentinel handling (`PipelineCreator.cpp:146-149`) is in the binding-merge stage,
  not the set-resolution walk — correctly out of scope.
