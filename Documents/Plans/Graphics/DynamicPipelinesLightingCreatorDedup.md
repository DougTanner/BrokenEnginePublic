<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:46:26.039Z","dependsOn":[]} -->
# DynamicPipelines Lighting Creator Dedup

## Context

Verified `/external-deep-analysis` finding (Sol-confirmed, baseline `944e41434c096cd1e2398b72961b2c3384ff859d`). `CreatePipelineLighting` (`Engine/Source/Graphics/Managers/DynamicPipelines.cpp:137-165`) and `CreatePipelineAxisAlignedLighting` (`DynamicPipelines.cpp:194-222`) are field-for-field identical — same idempotency guard, `CreateDynamicBuffer` call, pipeline flags `{kRenderTarget, kPushConstants, kIndirectHostVisible, kMax, kUpdateAfterBind}`, vertex buffer, render pass, extent, and descriptor-set shape — except for exactly four inputs: the `DynamicPipelineType` enum, the vertex-shader CRC, the fragment-shader CRC, and `kSamplerRepeat` versus `kSamplerClamp`. This is 16 cloned SLOC per body (clone group `5fbbac623b13`; file verbosity 0.294 versus corpus 0.039). The file already owns the house pattern for this situation: `CreateDepositPipeline` (`DynamicPipelines.cpp:253`) factors six analogous creators. Root `AGENTS.md` DRY directs extracting helpers for current duplication and keeping mirrored patterns parallel.

## Design

Add one private helper — for example `CreateAreaLightingPipeline(DynamicPipelineType eType, common::crc_t crc, std::string_view name, int64_t iBufferSize, common::crc_t vertexShaderCrc, common::crc_t fragmentShaderCrc, DescriptorFlags samplerFlag)` — containing the shared guard, dynamic-buffer creation, and `AddPipeline` body, mirroring the `CreateDepositPipeline` pattern. `CreatePipelineLighting` and `CreatePipelineAxisAlignedLighting` become one-call wrappers passing their four differing values; both public signatures stay unchanged. Every call site's `PipelineInfo` must be reproduced field-for-field (Vulkan pipeline state, descriptor layouts, render pass usage, and shader bindings are frozen). The Step-6 `/code-style-review` of the changed ranges also removes the mechanism-narrating comments inside the replaced bodies.

## Critical files

- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp` — `CreatePipelineLighting`, `CreatePipelineAxisAlignedLighting`, new private helper
- `Engine/Source/Graphics/Managers/DynamicPipelines.h` — private helper declaration

## In scope

- The two named creators, the new private helper definition and declaration, and comment cleanup within those replaced bodies

## Out of scope

- Every other creator, `CreateDepositPipeline` and its wrappers, `AddPipeline`, and the model-pipeline path
- Any change to Vulkan pipeline state, descriptor layouts, render pass usage, shader bindings, or the set of pipelines created (all frozen)
- Public signature changes

## Risk tier and invariants

Expected Change Workflow Tier 1. Trigger: mechanical, behavior-preserving local refactor with no public signature or invariant exposure. Invariants: both creators register the same pipelines under the same maps with byte-identical `PipelineInfo`; client-only file stays whole-file `BT_CLIENT`-guarded. Decisive check: the diff plus a client compile — each helper argument maps one-to-one onto the previously inlined value.

## Notes

Related same-file plan `Documents/Plans/Graphics/DynamicPipelinesSceneChunkValidation.md` touches the model-pipeline path only; the regions are disjoint, so there is no ordering constraint.
