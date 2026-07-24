<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-23T02:29:45.983Z","dependsOn":["Documents/Plans/Graphics/WindowedLightingDispatch.md"]} -->
# Reduce PipelineManager by extracting the world lighting/shadow pipeline family

## Context

The C++ review measured `Engine/Source/Graphics/Managers/PipelineManager.cpp` at 11,115 `bt-token-v1`, above the 10,000-token `.cpp` threshold. The contiguous lines 180-503 measure 5,004 tokens and contain one world lighting/shadow pipeline graph:

- `CreateLightingPipelines` (180-264, 1,147 tokens) creates deposit-fed spread, combine, and temporal accumulation; its successor will also register the prerequisite's bounded `LightingClear.comp` and `LightingHistoryCopy.comp` passes.
- `CreatePipelineShadows` (266-361, 1,288 tokens) creates the shadow elevation, world-shadow, world/object blur, temporal, and history-copy chain.
- `CreateLightingBlurPipelines` (363-391, 273 tokens) creates the two lighting-texture preblur passes consumed by the same lighting graph.
- `CreateLightingShadowDependentPipelines` (393-503, 2,296 tokens) creates Terrain, water displacement, and Water: the world-surface consumers of the lighting, shadow, object-shadow, elevation, smoke, ambient, and water-normal resources produced or fed by that graph.

The functions share the shader map, fixed-pipeline array, lighting-owned spread/combine/temporal state, water-normal pointer array, and render-target dependency surface. Moving all four creation stages together keeps a complete producer-to-consumer graph in one concrete component; moving smoke/wind, particles, debug, terrain-data, HDR, or descriptor verification with it would only stack unrelated responsibilities.

This is pre-existing structural debt discovered while reviewing the shadow-only work. It is outside that work's approved rendering intent and is not an acceptance failure. `WindowedLightingDispatch.md` is the directional prerequisite: it owns the remaining lighting work and itself depends on the active `WindowedLightingShadowDispatch.md`, so the active shadow change completes before this extraction starts.

This is a Tier 3 Change Workflow refactor. The component is private and client-only, all existing public fields and pipeline identities remain unchanged, and the manager is cold-path-only; nevertheless, adding a by-value helper changes `PipelineManager`'s object layout. Root tier rules classify data-layout exposure as Tier 3. There is no serialization, determinism/CRC, wire, replay, server, or asset-format exposure.

## Design

- Add client-only `Engine/Source/Graphics/Managers/WorldLightingShadowPipelines.h` and `.cpp`. `WorldLightingShadowPipelines` is a concrete by-value helper owned by `PipelineManager`, not a second global manager. Its constructor takes references/pointers to the existing shader map, fixed `mpPipelines` array, `mSpreadPipelines`, `mSpreadPipelineNames`, `mCombinePipeline`, `mLightingTemporalPipeline`, and `mppWaterNormalTextures`; it owns creation behavior but does not own or relocate any `Pipeline` or texture pointer storage.
- Give the component the four operations `CreateLightingPipelines`, `CreatePipelineShadows`, `CreateLightingBlurPipelines`, and `CreateLightingShadowDependentPipelines`. Move the complete current lines 180-503 into those same-named operations, preserving construction order, flags, shader CRCs, render targets, descriptor bindings, and current local descriptors. After `WindowedLightingDispatch.md` lands, move its two fixed registrations into the component's `CreateLightingPipelines`: `kPipelineLightingClear` for `LightingClear.comp` before deposit and `kPipelineLightingHistoryCopy` for `LightingHistoryCopy.comp` after temporal, with the prerequisite's bounded-image descriptor contracts unchanged. Do not leave either registration in `PipelineManager.cpp`.
- Adapt `ShadowBlurDesc` mechanically in the component: replace its `Pipelines ePipeline` member with `Pipeline& rPipeline`; initialize each entry from its corresponding fixed-array slot (`kPipelineShadowBlurH`, `kPipelineShadowBlurV`, `kPipelineObjectShadowsBlurH`, or `kPipelineObjectShadowsBlurV`) and call `rDesc.rPipeline.Create(...)`. Keep its flags, shader CRCs, input/output textures, and declaration order unchanged; no enum-to-component mapping remains unresolved.
- Keep every current `PipelineManager` public function, enum, and public state member in its current public region. Keep `CreateLightingPipelines`, `CreateLightingBlurPipelines`, `CreatePipelineShadows`, and `CreateLightingShadowDependentPipelines` as one-line delegating definitions in `PipelineManager.cpp`, so the constructor retains its lines 88-91 call sequence and no public signature changes. In `PipelineManager.h`, include the component header, then append an explicit `private:` region *after* existing public `mDynamicPipelines`; place only `WorldLightingShadowPipelines mWorldLightingShadowPipelines;` there. The new helper changes only private manager layout and cannot expose a new caller surface.
- `WorldLightingShadowPipelines.h` forward-declares `Pipeline`, `Shader`, and `Texture` and declares the reference-based constructor and four creation methods behind the existing whole-file `BT_CLIENT` guard. The component header does not include `PipelineManager.h`; its `.cpp` includes its own header, then `PipelineManager.h` for fixed enum slots and the current shader/texture dependencies. `PipelineManager.h` can therefore include the complete component type without a cycle, while the source uses the existing enum and PCH-provided Vulkan/global-manager declarations.
- Preserve all callers without migration. `CommandBufferRecordGlobal.cpp` continues to record the six world-shadow slots through its `pPipelines` reference; `CommandBufferRecordMain.cpp` continues to use object blur plus public spread/combine/temporal state; `GlobalUniforms.cpp` continues to write shadow indirect dispatches; `LightingUniforms.cpp` continues to populate spread indirect draws. This preserves descriptor-registration back-references and bindless elevation consumers, including `IslandTerrainResidency.cpp`'s registry lookup, because every `Pipeline` object stays at its current address.
- Run `/update-vcxproj` in fix mode for the two new files. Add the guarded `.cpp` and `.h` to the client `BrokenEngineSandbox.vcxproj` only, with `Engine\Graphics\Managers` filter entries; keep both out of the server project. The helper source and header use the same whole-file `BT_CLIENT` affinity as the manager.

Implement in this order so every intermediate edit remains comprehensible and buildable:

1. After the directional prerequisite lands, add the helper declaration/implementation and move its four current regions plus the prerequisite's clear/history-copy registrations into the matching component operation.
2. Wire the reference-based private helper into `PipelineManager`, retain its four public delegating wrappers, and confirm all fixed-array, spread, combine, temporal, and water-normal callers retain their existing member/slot spelling.
3. Reconcile Visual Studio membership and filters with `/update-vcxproj`, then run affected client compilation and the render/recreation smoke scenario.

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — retains constructor order, four delegating member definitions, and all non-world-lighting/shadow pipeline families.
- `Engine/Source/Graphics/Managers/PipelineManager.h` — retains all public state and adds only the private by-value helper after it.
- `Engine/Source/Graphics/Managers/WorldLightingShadowPipelines.h` and `.cpp` — own the cohesive lighting/shadow/world-surface creation graph and its client-only affinity.
- `Engine/Source/Graphics/Managers/CommandBufferRecordGlobal.cpp`, `CommandBufferRecordMain.cpp`, `Engine/Source/Graphics/Render/GlobalUniforms.cpp`, and `LightingUniforms.cpp` — read-only affected-site checks for unchanged fixed slot/state identity and recording/dispatch behavior.
- `Engine/Source/Frame/IslandTerrainResidency.cpp` and `Engine/Source/Graphics/Managers/TextureDescriptors.h` — read-only descriptor-registration checks: the shadow-elevation bindless consumer must retain the same `Pipeline*` identity.
- `Projects/BrokenEngineSandbox/Platforms/VisualStudio2026/BrokenEngineSandbox.vcxproj` and `.filters` — client-only membership for the added component files.

## Out of scope

- Any behavior change to shadow dispatch, blur, temporal/history, lighting clear/spread/combine/temporal, shaders, render-target layouts, descriptor flags, or command-buffer recording. This Plan moves the landed registrations intact; `WindowedLightingDispatch.md` remains the prerequisite behavioral owner.
- Pipeline descriptor registration ownership, descriptor-array sizing, bindless-slot lifecycle, pipeline enum values, array layout, and any caller API migration.
- Server code, simulation/determinism/CRC, serialization, save/replay, networking, asset packing, and unit tests.

## Acceptance criteria

- Current extraction arithmetic: 11,115 minus the measured 5,004-token region plus no more than 150 tokens for four wrappers/initializer leaves `PipelineManager.cpp` at about 6,260. Conservatively reserve 2,000 tokens for `WindowedLightingDispatch.md`'s clear/history-copy registrations and its descriptor reshaping: expected post-prerequisite manager size is at most about 8,260, leaving about 1,740 tokens of threshold headroom. The new component source is expected about 7,300 (5,004 moved + 2,000 prerequisite allowance + 300 component boilerplate) and its header about 700; all remain below their thresholds. Remeasure every changed/new C++ file after implementation; a prerequisite delta above the reserved 2,000 tokens requires re-estimation before moving code.
- Static affected-site search shows every world/object shadow record, spread/combine/temporal access, indirect-dispatch caller, and bindless descriptor consumer retains its existing public member or `mpPipelines` enum slot; no `Pipeline*` identity moves.
- `/update-vcxproj` verifies exactly one client `ClCompile`/`ClInclude` entry and one matching `Engine\Graphics\Managers` filter entry for each new component file, with no server entries.
- `/compile` performs an affected Debug|x64 client build after membership reconciliation, explicitly compiling `PipelineManager.cpp` and `WorldLightingShadowPipelines.cpp`; no unit tests.
- `/agent-harness` performs the existing client graphics/pipeline recreation smoke path after a normal rendered launch. Capture logs/screenshots sufficient to show the lighting clear/spread/combine/temporal/history-copy plus shadow elevation/world blur/temporal/history-copy and object-blur passes rebuild and render without Vulkan validation or descriptor-staleness errors.
- Run `/repo-code-review`, `/code-style-review`, and `/update-claude-docs` on the changed C++ scope; update guidance only if the extraction invalidates current ownership documentation.

## Notes

- Duplicate mapping: `Architecture_PipelineRegistrationOwnership.md` owns generic descriptor registration/unregistration; `PipelineDescriptorInfosRightSize.md` owns `PipelineInfo` storage; `WindowedLightingShadowDispatch.md` and `WindowedLightingDispatch.md` own shadow/lighting behavior. None owns this world lighting/shadow construction boundary.
- The metadata dependency is sufficient directional coordination. Do not edit the active shadow or windowed-lighting Plan files for this reduction; after their transitive completion, this plan is the sole owner of the extraction.
