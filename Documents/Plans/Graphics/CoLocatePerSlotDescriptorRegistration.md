# Co-Locate Per-Slot Descriptor Registration With Pipeline Declarations

## Context

Pipelines that consume bindless per-island texture arrays declare the array as
`{.ppTextures = gpTextureManager->mRenderTargetTextures.mElevationTextures.data(), ...}`
in `PipelineManager.cpp` (e.g., `kPipelineTerrainElevation` at line 388, and
the shared elevation descriptor at line 225). `PipelineDescriptorWriter::Write`
in `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp:446` auto-runs a
loop over `rDescriptorInfo.ppTextures[k]` and calls
`RegisterTextureBinding(arrayCrc = ppTextures[k]->mInfo.crc, ...)` for each
slot whose `Texture` already exists in `mTextureMap`.

That auto-registration keys on the *Texture's* CRC, not the *islandCrc* that
`IslandTerrain::AcquireTextureSlot` later uses as the binding key for
elevation. The mismatch means `UpdateArrayBindingsForKey(islandCrc)` cannot
find any of those auto-registered entries, so the per-slot descriptor patch is
re-done by hand in `AcquireTextureSlot` first-mint via explicit
`RegisterTextureBinding(islandCrc, &kPipelineTerrainElevation, ...)` and
(this session) `RegisterTextureBinding(islandCrc, &kPipelineShadowElevation, ...)`
calls. Every pipeline that shares `mElevationTextures` (or `mColorTextures`,
`mNormalsTextures`, `mAmbientOcclusionTextures`) needs its own bespoke
registration line inside `IslandTerrain.cpp` — and the list of pipelines lives
in `PipelineManager.cpp`, with no compile-time link between the two.

Commit `09fb128` collapsed an earlier per-mint pipeline-rebuild kludge and
silently dropped `kPipelineShadowElevation` from the registration list because
the registration list is hand-authored in a different file from the pipeline
declarations. Shadow elevation was bound to slot-0 placeholder for every
island until the regression was traced back to the missing registration. The
fix in this session bolts a second registration line onto the first; the next
pipeline added against `mElevationTextures` (e.g., a future bloom or
post-process pass) will need a third line, and the next person to refactor
either file may again forget one.

## Design

Investigate whether per-slot island-keyed registration can be **derived from
the pipeline declaration** rather than hand-authored at the
`AcquireTextureSlot` call site. Three shapes worth comparing before
implementing:

- **Option A — Tag the descriptor info with an island-key callback.**
  Extend `DescriptorInfo` (or its descriptor-array variant) with an optional
  `std::function<common::crc_t(int64_t iSlot)>` or function pointer that
  resolves a slot index to its binding key. `PipelineDescriptorWriter::Write`
  calls it instead of (or alongside) `ppTextures[k]->mInfo.crc`. Pipelines
  that use island-keyed arrays declare the callback in
  `PipelineManager.cpp`; `AcquireTextureSlot` then only needs to call
  `UpdateArrayBindingsForKey(islandCrc)` after creating the real Texture.
- **Option B — Group elevation pipelines under a single binding key.**
  `kPipelineTerrainElevation` and `kPipelineShadowElevation` both bind the
  same `mElevationTextures` array. Introduce a small registry (e.g.,
  `mElevationConsumerPipelines[]` in `TextureDescriptors`) populated at
  `PipelineManager` init from the pipeline-declaration data. `AcquireTextureSlot`
  iterates that registry instead of naming each pipeline literally.
- **Option C — Move the registration loop into `PipelineManager` ctor.**
  After every elevation-consuming pipeline is created, sweep `mIslands` and
  register each existing template. `IslandTerrain` then only registers
  *new* templates against the *existing* pipeline set, and the
  pipeline list is the single source of truth.

Choose after reading `PipelineDescriptorWriter::Write` end-to-end and
auditing the existing `RegisterTextureBinding` call sites — currently five in
`IslandTerrain::AcquireTextureSlot` (elevation x2, color, normals, AO) plus
the auto-loop in `PipelineDescriptorWriter`. Pick the option that yields one
authoritative list of (array, binding-key-fn, consumer-pipelines).

## Out of scope

- Refactoring `TextureDescriptors`' binding-map data structure (key/value
  shape, allocation strategy). The map is fine; only the *registration list*
  is split.
- The other three per-chunk-CRC registrations (`kPipelineTerrainColor`,
  `kPipelineTerrainNormal`, `kPipelineTerrainAmbientOcclusion`) — these key
  on `textureCrcs[N]` which are real `Texture::mInfo.crc` values, so the
  `PipelineDescriptorWriter` auto-path already covers them once their
  Textures land in `mTextureMap`. Verify before assuming, but do not
  restructure them as part of this plan unless the verification proves
  otherwise.
- Compute-pipeline descriptor registration (see
  `Engine/Architecture_MultiSetComputePipelines.md`).
- `UpdateArrayBindingsForKey` itself — the dispatch sink stays.

## Acceptance criteria

- Adding a new graphics pipeline that consumes `mElevationTextures` (or any
  other island-keyed bindless array) requires editing only one file, and no
  edit to `IslandTerrain::AcquireTextureSlot`.
- Removing the hand-authored `RegisterTextureBinding(islandCrc, &kPipelineXxx, ...)`
  block from `AcquireTextureSlot` for elevation does not regress terrain or
  shadow rendering.
- Code-walk confirms the pipeline declaration in `PipelineManager.cpp` is the
  single source of truth for the (array, binding-key, consumer) triple for at
  least the elevation array.

## Critical files

- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — pipeline
  declarations with `ppTextures` references (lines 225, 388 today).
- `Engine/Source/Graphics/Objects/PipelineDescriptorWriter.cpp:446` — the
  auto-registration loop that already exists for `ppTextures[k]->mInfo.crc`
  keying; extend or repurpose for island-keyed arrays.
- `Engine/Source/Graphics/Managers/TextureDescriptors.{h,cpp}` —
  `RegisterTextureBinding` signature, `mTextureBindings` storage, the
  `UpdateArrayBindingsForKey` dispatch.
- `Engine/Source/Frame/IslandTerrain.cpp::AcquireTextureSlot` — the
  hand-authored registration block (currently 5 calls) that should collapse
  to a `UpdateArrayBindingsForKey(islandCrc)` after first-mint.

## Notes

- This is the architectural fix for the bug class that produced the
  shadow-elevation regression. The shadow-elevation registration fix landed
  this session is the minimal point-fix; this plan is the durable answer.
- Coordinate with `Documents/Plans/Graphics/DynamicIslandLoadingFollowups.md`
  Follow-up 1 — both touch `AcquireTextureSlot` first-mint paths. Land the
  device-lost reset first (it is user-visible and time-critical), then this
  plan can simplify the call site.
- Validate the assumption that the per-chunk-CRC registrations
  (color/normals/AO) really are covered by `PipelineDescriptorWriter`'s
  auto-loop before deleting them. If `mTextureMap.contains(arrayCrc)` is
  empty at pipeline-creation time (the chunk Textures haven't loaded yet),
  the auto-loop skips the slot and the hand-authored fallback in
  `AcquireTextureSlot` is doing real work — in which case Options A or B are
  the right shapes and Option C is wrong.
