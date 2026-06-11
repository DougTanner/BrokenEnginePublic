# Refactor: Texture Cluster Decomposition (Graphics/Managers)

## Context

Source: /external-refactor-clean on `Engine/Source/Graphics/Managers` (non-recursive). In-function structure
items in the texture cluster: oversized functions with clean seams, verbatim duplication, deep nesting, and a
mode-encoding parameter list. All boot/recreate or adoption-frame paths — no hot-path pressure; value is
readability and drift-resistance.

## Design

### Engine/Source/Graphics/Managers/TextureManager.cpp
- Ctor (`TextureManager.cpp:71-349`, 278 lines): (a) the five island-placeholder `Create` blocks
  (`:132-238`) are near-identical ~25-line stanzas differing only in name/format/one pixel value — extract a
  helper taking `(name, format, pixel-writer)`; (b) the acquire command-pool + command-buffer block
  (`:313-333`) is duplicated **verbatim** in `CreateScreenDependentResources` (`:382-401`) — extract
  `CreateAcquireCommandBuffers()` called from both. [~45m]
- `ProcessPendingTextures` (`:603-708`): nesting reaches 4; extract the per-chunk adoption body (`:625-673`)
  into an `AdoptUploadedChunk(...)` helper (the lazy begin-command-buffer becomes a small
  `EnsureAcquireCommandBufferBegun()`). Pairs with the pending-counter early-out from
  `Refactor_PerFramePerf.md`. [~30m]
- Optional: collapse the eight `VkSampler` members (`TextureManager.h:117-124`) + 8-branch `GetSampler`
  (`:565-600`) + 16-line `DestroySamplers` (`:404-422`) into a `VkSampler mpSamplers[count]` indexed by a
  small enum (also turns `GetSampler`'s mutual-exclusion sum at `:555-563` into a table lookup). Take only if
  the sampler code is being touched anyway. [~1h, optional]

### Engine/Source/Graphics/Managers/TextureDescriptors.cpp / .h
- `RegisterTextureBinding` (`TextureDescriptors.h:22`, impl `:248`): 8 parameters, three defaulted tails
  encoding three mutually-exclusive binding shapes (single texture / array / single element). Replace with a
  `TextureBindingInfo` struct + designated initializers — call sites in PipelineDescriptorWriter /
  IslandTerrain become self-documenting. [~1h]
- `RewriteSamplerDescriptors` (`:328-388`): nesting reaches 5; the three top-level phases (standalone
  samplers / per-CRC bindings / live bindless arrays) are already commented seams — extract the
  single-texture rewrite (`:348-371`). Settings-change path only. [~30m]

### Engine/Source/Graphics/Managers/TextureUploadManager.cpp
- `UploadThread` (`:147-426`, ~280 lines, nesting ≥4): five clean comment-separated seams — dequeue
  (`:164-171`), early-outs (`:175-192`), first-chunk image create (`:208-233`), staging fill + copy record
  (`:265-336`), final-barrier + submit + finish (`:338-412`). Extract each into private methods; the
  in-progress state already lives on the class, so the split is mechanical. [~1h]

## Critical files
- `Engine/Source/Graphics/Managers/TextureManager.cpp`, `TextureManager.h`
- `Engine/Source/Graphics/Managers/TextureDescriptors.cpp`, `TextureDescriptors.h`
- `Engine/Source/Graphics/Managers/TextureUploadManager.cpp`, `TextureUploadManager.h`
- Call sites of `RegisterTextureBinding` (PipelineDescriptorWriter, `Engine/Source/Frame/IslandTerrain.cpp`)

## Out of scope
- `BlurLightingTexture` (103 lines) — allocation-compliant and by-design blocking; decomposition is
  low-value, deliberately skipped.
- The `RegisterTextureBinding` *consumer registry semantics* — unchanged; only the call signature shape moves.
- `TryLoadCachedTexture`'s 8-parameter list — that function is being rewritten by
  `Graphics/TextureCacheValidatePayloadSize.md`; not touched here.
- Behavior changes of any kind — pure extraction/struct-ification.

## Acceptance criteria
- Client builds clean and renders identically; no function in the cluster exceeds ~100 lines except the
  declarative remainder of the TextureManager ctor.

## Notes
- No determinism/CRC exposure. If `Architecture_BindlessSlotLifecycle.md` executes, it reshapes
  `TextureDescriptors` — land this plan's `TextureDescriptors` items either before it (small, mechanical) or
  fold them into it; do not run both concurrently (Dependencies entry).
