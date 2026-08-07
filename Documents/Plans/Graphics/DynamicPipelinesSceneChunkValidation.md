<!-- broken-engine-plan/v1 {"createdUtc":"2026-08-07T13:46:24.268Z","dependsOn":[]} -->
# DynamicPipelines Scene Chunk Validation

## Context

Verified `/external-deep-analysis` finding (Sol-confirmed, baseline `944e41434c096cd1e2398b72961b2c3384ff859d`). `Engine/Source/Graphics/Managers/AGENTS.md` (Shared Contracts, pack trust-boundary rule) requires pack-backed assets to be validated against the resident chunk, with boot consumers throwing `common::CorruptStreamException`. The model-pipeline creation path violates that contract:

- `ResolveModelChunkShaders` (`Engine/Source/Graphics/Managers/DynamicPipelines.cpp:21-26`) reads `gpFileManager->GetEagerChunkMap().at(sceneCrc)` and consumes pack-derived `sceneHeader.modelCrc` and `sceneHeader.bHasAnimation` without validating that the chunk exists or is a scene chunk.
- `gpBufferManager->mModelMap.at(modelCrc)` (`DynamicPipelines.cpp:82` and `:121`) resolves the pack-derived model reference before `ModelPipeline::Create` runs, outside the `CorruptStreamException` logging boundary at `DynamicPipelines.cpp:44-53`.
- Nothing upstream enforces scene-to-model references: `BufferManager` inserts every eager `kModel` chunk without cross-checking scene references (`Engine/Source/Graphics/Managers/BufferManager.cpp:42`), and `PackChunks` populates the eager map without enforcing them (`Engine/Source/File/PackChunks.cpp:335`).

A corrupt or incomplete pack whose scene chunk is absent, has the wrong kind, or references a missing model therefore throws `std::out_of_range` with no asset diagnostic, instead of the required `CorruptStreamException` carrying the scene CRC. Callers pass generated internal scene CRC constants (for example `Projects/BrokenEngineSandbox/Source/Frame/Collections/Missiles/MissilesRender.cpp:16`), so the exposure is limited to corrupt/incomplete pack content — but pack content is exactly the trust boundary the contract governs.

## Design

Validate every pack-derived resolution on this path before any `PipelineInfo` consumes it, converting each failure into `common::CorruptStreamException` so it flows through the existing corruption logging boundary:

- In `ResolveModelChunkShaders`, replace the unchecked `GetEagerChunkMap().at(sceneCrc)` with a lookup that throws `CorruptStreamException` (naming the scene CRC) when the chunk is absent or its kind is not a scene chunk, and validate that the header's `modelCrc` resolves in `gpBufferManager->mModelMap` before returning it — throwing `CorruptStreamException` on a miss.
- Move the corruption logging boundary up: each public creator (`CreateModelPipeline(common::crc_t, ...)` and `CreateModelPipelineShadow`) wraps its resolution and creation — `ResolveModelChunkShaders`, the `gpBufferManager->mModelMap` lookup, and the `CreateModelPipeline(const ModelPipelineSpec&)` call — in one `try`/`catch (const common::CorruptStreamException&)` that logs the existing `kError` diagnostic (same wording and allocation-free formatting, parameterized by the shadow flag as today) and rethrows. `CreateModelPipeline(const ModelPipelineSpec&)` itself no longer catches; its contract comment moves with the boundary. Every corruption case then logs once with the scene CRC and propagates to `MainThread`'s handler, matching the boot hard-fail tier documented at `DynamicPipelines.cpp:41-43`.
- Valid-pack behavior is unchanged: successful lookups produce byte-identical `PipelineInfo` values and identical pipeline registration.

## Critical files

- `Engine/Source/Graphics/Managers/DynamicPipelines.cpp` — `ResolveModelChunkShaders`, `CreateModelPipeline` (both overloads), `CreateModelPipelineShadow`
- `Engine/Source/Graphics/Managers/DynamicPipelines.h` — unchanged (no signature or declaration changes)

## In scope

- Validation and `CorruptStreamException` conversion in `ResolveModelChunkShaders` and the model-pipeline creation path (`CreateModelPipeline` overloads, `CreateModelPipelineShadow`), including relocating the corruption logging boundary into the two public creators as decided in `## Design`

## Out of scope

- Pack format, DataPacker export, or `kiVersion` changes
- `BufferManager`/`PackChunks` population and any upstream cross-reference enforcement
- The per-frame texture-adoption soft-fail path
- Any Vulkan pipeline state, descriptor layout, render pass, shader binding, or the set of pipelines created (all frozen)

## Risk tier and invariants

Expected Change Workflow Tier 2. Trigger: scoped client boot behavior at an existing pack trust boundary; no determinism/CRC, wire, serialization, or layout exposure (precedent: `Documents/Plans/DataPacker/GltfLoaderInputHandling.md`). Invariants: valid packs behave identically; boot consumers of pack data throw `common::CorruptStreamException` per `Engine/Source/Graphics/Managers/AGENTS.md`; client-only file stays whole-file `BT_CLIENT`-guarded.

## Acceptance criteria

- An absent scene chunk, a wrong-kind chunk, or a missing referenced model on this path each throw `common::CorruptStreamException`, logged once at `kError` with the scene CRC, before any `PipelineInfo` field is populated; no `std::out_of_range` escapes this path for pack-derived lookups.
- With a valid pack, client boot and pipeline registration behave identically (client compile plus normal harness boot is decisive).

## Notes

Sol verification also confirmed the `GetEagerChunkMap().at(sceneCrc)` miss case shares the exception-boundary problem; it is included above. `mrShaders.at(...)` lookups keyed by generated compile-time `data::` constants are internal, not pack-derived, and stay unchanged.
