# Architecture: Objects Dead Recreate APIs + Invariant Docs

## Context

Source: /external-architecture-review on `Engine/Source/Graphics/Objects` (non-recursive). Two recreate
APIs have zero callers repo-wide, and both would walk straight into the registration-lifecycle asymmetry
(see `Architecture_PipelineRegistrationOwnership.md`) if ever called: `TextureDescriptors` holds raw
`Pipeline*` back-references that only whole-`PipelineManager` reconstruction clears, so any out-of-rebuild
recreate replays stale state and duplicates registrations. Deleting the dead APIs removes the only code
paths that could violate the invariant; the companion items write the convention-held invariants down where
a future change would silently break them.

## Design

### Engine/Source/Graphics/Objects/ModelPipeline.h / ModelPipeline.cpp
- Delete `ModelPipeline::Recreate()` (`ModelPipeline.h:24`, `ModelPipeline.cpp:89`) — zero callers
  (grep `(\.|->)Recreate\(` repo-wide). It replays `mInfoTemplate`'s raw `Shader*`/`Buffer*`/`Texture**`
  snapshot (`ModelPipeline.h:41`) and would re-`push_back` duplicate `Pipeline*` registrations into
  `gpTextureManager->mTextureDescriptors`. Also delete the snapshot members `mInfoTemplate` /
  `mbAddModelDescriptors` / `mbIsShadow` (`ModelPipeline.h:38-43`) and their writes
  (`ModelPipeline.cpp:8-13`) — `Create` only *writes* them; `Recreate` (`ModelPipeline.cpp:91`) is their
  sole reader (grep-verified), so they die with it. [~10m]

### Engine/Source/Graphics/Objects/Texture.h / Texture.cpp
- Delete `Texture::ReCreate()` (`Texture.h:104`, `Texture.cpp:160`) — zero callers (grep
  `(\.|->)ReCreate\(` repo-wide). The pipeline-tier full-manager drop made it redundant. [~5m]

### Engine/Source/Graphics/Objects/CLAUDE.md (+ registration-site comments)
- Document the rebuild-only lifecycle invariant: pipelines may only be (re)created during whole
  `PipelineManager` reconstruction, because `PipelineDescriptorWriter::Write` registers raw `Pipeline*`
  into `gpTextureManager->mTextureDescriptors` (`PipelineDescriptorWriter.cpp:475,494,514`) and the only
  cleanup is `ClearTextureBindings()` at manager rebuild (`PipelineManager.cpp:62`,
  `TextureDescriptors.cpp:390-395`) — `Pipeline::Destroy` (`Pipeline.cpp:155`) does not unregister. Add a
  one-line comment at the registration block and the CLAUDE.md sentence. [~10m]
- Document the destroy discipline: `Buffer::Destroy`/`Texture::Destroy`/`Pipeline::Destroy` are immediate;
  callers own the fence discipline (the three manager-side strategies: full-drain destroy, one-frame buffer
  parking in `BufferManager::GrowMeshDataBuffer`, the RenderGlobal descriptor-patch safety window). One
  CLAUDE.md sentence: never `Destroy` a possibly-in-flight resource outside a drained window. [~5m]

### Engine/Source/Graphics/Objects/CommandBuffers.h
- Add a thread-contract comment on `mFlags`/`mVkFence`: three writers (`kExecuted` set on the
  `mSubmitGlobal` worker `CommandBufferManager.cpp:111`, fence reset on `mSubmitMain` `:168`, read back on
  the main thread `Graphics.cpp:157,216`) are sequenced only by the `PersistentWorker` Wake/Wait chain —
  non-atomic by design, not thread-safe for new callers. [~5m]

### Engine/Source/Graphics/CLAUDE.md
- Extend the guard-scope note (`Graphics/CLAUDE.md:19`) to mention that all of `Objects/*` is unwrapped
  client-only code (client vcxproj membership + the `Engine.h:32-37` `BT_CLIENT` span) — today the list
  enumerates only the top-level unwrapped files, so a future agent has no local warning. [~5m]

## Critical files
- `Engine/Source/Graphics/Objects/ModelPipeline.h`, `ModelPipeline.cpp`
- `Engine/Source/Graphics/Objects/Texture.h`, `Texture.cpp`
- `Engine/Source/Graphics/Objects/CommandBuffers.h`
- `Engine/Source/Graphics/Objects/CLAUDE.md`, `Engine/Source/Graphics/CLAUDE.md`

## Out of scope
- Making the registration lifecycle actually symmetric — `Graphics/Architecture_PipelineRegistrationOwnership.md`.
- Any reusable single-pipeline/texture recreate capability (would be a Feature; the registration plan is
  its prerequisite).
- The `BT_CLIENT` guard-policy wording itself — `Common/AggregationAndScopeRuleQualifiers.md` (decision plan).

## Acceptance criteria
- Zero references to `Recreate`/`ReCreate` remain; client builds clean; the invariants are stated at the
  registration block, in `Objects/CLAUDE.md`, and in the `Graphics/CLAUDE.md` guard note.

## Notes
- Deletion gate: re-grep both symbols at execution; remove-vs-keep is pre-staged for `/external-grill-plan`
  per the never-remove-features rule (mirrors `Architecture_DebugRenderDeadPrimitives.md`). Check
  `Objects/CLAUDE.md:11`'s claim that `Recreate()` exists for settings recreate — the real settings path
  drops the whole `PipelineManager`.
- Client-only; no determinism/CRC, `kiVersion`, replay, or network exposure. Code change is pure deletion +
  comments.
- Coordination: deleting the snapshot bools shrinks `Graphics/Refactor_ModelPipelineMembers.md`'s
  bool→Flags conversion to the two surviving members — land this first or refresh that plan's member list.

## Verification Notes
Verified: all cited paths/lines/symbols re-checked against source.
- Dead-API claim re-grepped: `(\.|->)Recreate\(` / `(\.|->)ReCreate\(` return zero matches repo-wide;
  definitions confirmed at `ModelPipeline.h:24` / `ModelPipeline.cpp:89` and `Texture.h:104` /
  `Texture.cpp:160-166`.
- Corrected the original "Keep `mInfoTemplate` itself (used by `Create`)" — false: `Create` only writes the
  snapshot (`ModelPipeline.cpp:11-13`); `Recreate` (`:91`) is the sole reader of `mInfoTemplate`,
  `mbAddModelDescriptors`, and `mbIsShadow`. All three members are deleted with the API (bigger cleanup
  than originally scoped; still pure deletion).
- Registration/lifecycle citations exact: `RegisterTextureBinding`/`try_emplace` at
  `PipelineDescriptorWriter.cpp:475,494-495,514` (plus the IBL trio `:103,:125,:146` and `:480,:509`),
  `ClearTextureBindings()` at `PipelineManager.cpp:62` and `TextureDescriptors.cpp:390-395`,
  `Pipeline::Destroy` at `Pipeline.cpp:155-210` with no unregistration.
- Thread-contract citations exact: `kExecuted` set at `CommandBufferManager.cpp:111`
  (`SubmitGlobalToQueue`, runs on `mSubmitGlobal` when `kbRenderThread`), fence reset at `:168`
  (`SubmitMainToQueue` on `mSubmitMain`), main-thread reads at `Graphics.cpp:157,216`.
- `Objects/CLAUDE.md:11` confirmed to carry the "Recreate() — required after any auto-appended RTT texture
  is destroyed/recreated" claim that must be rewritten when the API is deleted; the real settings path
  drops the whole `PipelineManager` (`Graphics/CLAUDE.md` Destroy/Refresh).
- `Graphics/CLAUDE.md:19` is the guard-scope bullet; the Objects includes sit at `Engine.h:32-37` inside
  the `BT_CLIENT` span opened at `Engine.h:21`.
