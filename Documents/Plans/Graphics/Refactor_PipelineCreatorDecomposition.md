# Refactor: PipelineCreator Staged Decomposition

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Objects` (non-recursive). `PipelineCreator.cpp`
(722 lines) holds the directory's two largest functions: `CreateGraphicsPipeline` at 362 lines (~200 of
them default-initialized `Vk*CreateInfo` scaffolding) and `CreateDescriptorSetLayouts` at 119 lines with
three distinct stages. `SetupIndirectBuffer`/`CreateDescriptorSetLayouts` are already extracted as named
stages — this completes the pattern. Bundled here: both helper structs take a redundant `PipelineInfo`
parameter that aliases `rPipeline.mInfo`, and internals mix the two names arbitrarily.

## Design

### Engine/Source/Graphics/Objects/PipelineCreator.cpp
- Decompose `PipelineCreator::CreateGraphicsPipeline` (`:237-598`, 362 lines) into named stages. All
  `Vk*StateCreateInfo` structs must outlive the `vkCreateGraphicsPipelines` call (`:596`), so introduce a
  local `GraphicsPipelineState` aggregate owning the eight state structs + viewport/scissor/blend
  attachments, then extract static helpers writing into it: `ConfigureVertexInput` (`:460-482`),
  `ConfigureViewportScissor` (`:484-496`), `ConfigureBlendState` (`:498-521` plus the MRT replication block
  `:574-594`), `ConfigureRasterization` (`:523-550` — wireframe/cull/depth-bias),
  `ConfigureMultisampling` (`:555-569`). `CreateGraphicsPipeline` becomes an ~60-line orchestrator matching
  the existing extracted-stage style. [~1h]
- Split `CreateDescriptorSetLayouts` (`:117-235`, 119 lines) along its three existing stages into named
  statics: merge vertex+fragment reflected bindings (`:124-154`), split by set index (`:162-190`), create
  layouts + pipeline layout (`:192-234`). [~30m]

### Engine/Source/Graphics/Objects/PipelineCreator.h + PipelineDescriptorWriter.h
- Drop the redundant `const PipelineInfo&` parameter from `CreateGraphicsPipeline`/`CreateComputePipeline`
  (`PipelineCreator.h:11-12`) and `PipelineDescriptorWriter::Write` (`PipelineDescriptorWriter.h:16`) —
  every call site passes `rPipeline.mInfo` itself (`Pipeline.cpp:137,141,144`), and internals mix the two
  aliases (`PipelineCreator.cpp:486-487` reads `rPipeline.mInfo.flags`, `:498` reads `rPipelineInfo.flags`,
  and the `:523-536` block reads `rPipeline.mInfo` at `:526` then `rPipelineInfo` at `:536`). Read
  `rPipeline.mInfo` throughout; the cascade reaches `WriteModelDescriptor` / `BindingIsInSet0` /
  `FilterWritesByShaderLayout` / `RouteWritesBySet`, plus the static `SetupIndirectBuffer`
  (`PipelineCreator.cpp:82`, name reads `:89,:113`) and `CreateDescriptorSetLayouts` (`:117`). [~20m]

## Critical files
- `Engine/Source/Graphics/Objects/PipelineCreator.h`, `PipelineCreator.cpp`
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.h`, `PipelineDescriptorWriter.cpp`
- `Engine/Source/Graphics/Objects/Pipeline.cpp` (call sites)

## Out of scope
- `Texture::CreateRenderTarget` (119 lines, single linear declarative flow) — readable as written, not
  filed.
- The set-index resolver dedup — `Graphics/Architecture_DescriptorSetIndexDedup.md` (same file;
  co-schedule).
- The `Write`-path decomposition — `Graphics/Refactor_WriteDescriptorDecomposition.md` (land with or
  before this plan; the parameter drop touches its signatures).
- Any pipeline-state value change — extraction must be bit-identical (same struct contents reaching
  `vkCreateGraphicsPipelines`).

## Acceptance criteria
- No function in `PipelineCreator.cpp` exceeds ~120 lines; the `PipelineInfo` alias parameter is gone from
  both helper headers; client builds clean; boot + settings-recreate smoke renders identically.

## Notes
- Pure mechanical extraction on the cold creation path; client-only, no determinism/CRC, `kiVersion`,
  replay, or network exposure. The state-struct lifetime constraint is the one execution hazard — the
  aggregate keeps everything alive through the create call.
- Sequencing: `Graphics/WaterDisplacementIndirectCompute.md` adds a branch at `PipelineCreator.cpp:621-624`
  (`CreateComputePipeline`'s `kIndirectHostVisible` `ASSERT(false)`) — land one before the other and refresh
  citations (see Order.md File Groups; that plan's own `:611-614` citation has already drifted).
- No grill decisions.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source (file is 722 lines as claimed).
- `CreateGraphicsPipeline` spans `:237-598` (362 lines; `:241-444` is the default-init `Vk*CreateInfo`
  scaffolding); `CreateDescriptorSetLayouts` spans `:117-235` (119 lines) with the three stages exactly as
  cited (`:124-154` merge, `:162-190` set split, `:192-234` layout creation).
- Helper extraction ranges exact: vertex input `:460-482`, viewport/scissor `:484-496`, blend `:498-521`
  + MRT replication `:574-594`, rasterization (wireframe/cull/depth-bias) `:523-550`, multisampling
  `:555-569`. The depth-stencil pair (`:552-553`) stays in the orchestrator. Lifetime constraint confirmed:
  every state struct is referenced by `vkGraphicsPipelineCreateInfo` (`:425-444`) and must outlive
  `vkCreateGraphicsPipelines` at `:596` — the `GraphicsPipelineState` aggregate is the right shape.
- Redundant-parameter claim exhaustively verified: repo grep shows the only callers of
  `CreateGraphicsPipeline` / `CreateComputePipeline` / `Write` are `Pipeline.cpp:137,141,144`, each passing
  `mInfo` (i.e. `rPipelineInfo` and `rPipeline.mInfo` alias the same object); `TextureDescriptors.cpp` uses
  only `BindingExistsInShaderLayout` (`:253,:272`), which already takes no `PipelineInfo`. Signatures at
  `PipelineCreator.h:11-12` and `PipelineDescriptorWriter.h:16` as cited. Corrected the `:526` citation
  (that line reads `rPipeline.mInfo` only; the mixing is `:526` vs `:536`) and added the two static helpers
  in this TU that also take the alias parameter, so the cascade list is complete.
