# Elevation MAX-Blend Format-Feature Guard

## Context

The "Island Overlap Elevation MAX" change added the `kMax` pipeline flag (→ `VK_BLEND_OP_MAX`) to both
`kPipelineTerrainElevation` and `kPipelineShadowElevation` (`Engine/Source/Graphics/Managers/PipelineManager.cpp`,
the `.flags = {kRenderTarget, kPushConstants, kMax, kUpdateAfterBind}` lines). Those passes now MAX-blend the
per-island heightmaps into the composite elevation RTTs `mTerrainElevationTexture` / `mShadowElevationTexture`
(`Engine/Source/Graphics/Managers/RenderTargetTextures.cpp`, both `.Create({...})` calls), which are created with
`shaders::keElevationFormat`.

`keElevationFormat` is **`VK_FORMAT_R16_SFLOAT`** (`Engine/Data/Shaders/ShaderLayoutsBase.h:35`). The per-island
*sampled source* heightmap array is `R32_SFLOAT`, but that array is only ever sampled — never blended — so its
blend support is irrelevant; the format that is now blended is the R16_SFLOAT composite color attachment.

Enabling blend (`blendEnable = VK_TRUE`, which the `kMax` path sets via `PipelineCreator`) on a color-attachment
format requires the device to advertise `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` in that format's
`optimalTilingFeatures`. There is currently **no capability check** for this. The only adjacent probe,
`SupportsLinearFilter` (`Engine/Source/Graphics/GraphicsUtils.cpp:98`), queries a different bit
(`VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`) and is used only by `TextureManager::CreateSamplers`
(`Engine/Source/Graphics/Managers/TextureManager.cpp:427`) to downgrade R32_SFLOAT samplers to NEAREST. Nothing
verifies R16_SFLOAT blendability.

R16_SFLOAT color-attachment blend support is near-universal on desktop GPUs, so this is not expected to fire in
practice. But the dependency is now real and unguarded: a device that supports R16_SFLOAT as a color attachment
but *not* as a blendable one would hit undefined behavior / a validation error at pipeline creation with no
graceful fallback and no diagnostic pointing at the cause.

This was surfaced by the audit of the elevation-MAX change (its own risk note flags R16_SFLOAT blend support as
unverified) and deferred as out of scope — it is producer/device-setup hardening, not part of the compositing
fix itself.

## Problem

No device-capability check that `keElevationFormat` (`VK_FORMAT_R16_SFLOAT`) advertises
`VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` before the elevation pipelines enable MAX blending on it. On a
non-conforming device the failure is silent/undefined rather than a clear, logged precondition violation.

## Proposed change

Mirror the existing format-feature probe pattern at device/format setup time and fail loud if the bit is absent:

1. Add a small query helper alongside `SupportsLinearFilter` in `Engine/Source/Graphics/GraphicsUtils.{h,cpp}` —
   e.g. `bool SupportsColorAttachmentBlend(VkFormat vkFormat)` — that calls
   `vkGetPhysicalDeviceFormatProperties(gpInstanceManager->mVkPhysicalDevice, vkFormat, &props)` and returns
   `(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) != 0`. This is the exact shape
   of the existing `SupportsLinearFilter` body (line-for-line, only the queried bit differs), and matches the
   inline probes already in `InstanceManager::SelectDepthFormat`
   (`Engine/Source/Graphics/Managers/InstanceManager.cpp:621-663`, which test
   `VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT`).
2. Call it once at device/format setup and ASSERT (and `LOG(kGraphics, kError, ...)`) if it returns false, naming
   `keElevationFormat` and the MAX-blend dependency so the failure is self-describing. Two reasonable placements
   (pick the simplest at implementation time):
   - In `InstanceManager` right after `SelectDepthFormat()` (`InstanceManager.cpp:357-358`, the canonical
     format-selection point) — but `keElevationFormat` is a fixed compile-time constant, not a selected member,
     so this is a pure precondition check rather than a selection.
   - Where the elevation RTTs are created (`RenderTargetTextures::CreateShadowTextures` /
     `CreateTextures`, the two `keElevationFormat` `.Create` calls) — closest to the actual blended attachment,
     and runs on every recreate.

   Prefer the device-setup placement so the check runs once at boot and is not tied to RTT recreation churn.

The intent is fail-loud, not graceful degradation: an ASSERT/error log is sufficient because the format is fixed
and the bit is near-universal. The heavier alternatives below are noted only so a future implementer knows the
escape hatch if the assert ever fires on a real device.

## Critical files

- `Engine/Source/Graphics/GraphicsUtils.{h,cpp}` — add `SupportsColorAttachmentBlend(VkFormat)` mirroring
  `SupportsLinearFilter` (`GraphicsUtils.cpp:98-103`).
- `Engine/Source/Graphics/Managers/InstanceManager.cpp` — preferred call site, right after the
  `SelectSurfaceFormat()` / `SelectDepthFormat()` sequence (`:357-358`); follows the existing
  `vkGetPhysicalDeviceFormatProperties` usage in `SelectDepthFormat` (`:621-663`).
- `Engine/Data/Shaders/ShaderLayoutsBase.h` — `keElevationFormat` (`:35`), the format the check targets.
- `Engine/Source/Graphics/Managers/RenderTargetTextures.cpp` — the two elevation RTT `.Create` calls
  (`mShadowElevationTexture` `:70`, `mTerrainElevationTexture` `:327`); alternate call site and the consumer the
  guard protects.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — the two `kMax` elevation pipelines
  (`kPipelineShadowElevation` `:212`, `kPipelineTerrainElevation` `:482-499`) that introduced the blend dependency.

## Out of scope

- The `R32_SFLOAT` per-island *sampled* heightmap array — it is never blended; its only device-capability concern
  (linear-filter support) is already handled by `SupportsLinearFilter` in `TextureManager::CreateSamplers`.
- Any change to the blend math, the `kMax` flag, the clear values, or the compositing behavior of the elevation
  prepasses — the MAX change itself is correct; this plan only adds a guard.
- Graceful fallback / format substitution — not implemented now. See the heavier-alternatives note below; only
  pursue if the assert ever fires.
- The shared CPU `GlobalElevation` MAX path (no GPU format involvement); determinism and `kiNavDataVersion` are
  unaffected — this is a pure client-side device-setup check.

## Acceptance criteria

- A device-capability check for `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` on `keElevationFormat` runs at
  boot (or RTT creation) and, when the bit is absent, fires an ASSERT and logs an error that names the format and
  the MAX-blend elevation dependency.
- On conforming hardware (the expected case) the check is silent and adds no per-frame cost.
- No behavior change to the elevation compositing on hardware that advertises the bit.

## Notes

- Pure producer / device-setup hardening: client-only (Graphics is client-only), no determinism impact, no
  format/version impact, no `kiNavDataVersion` bump.
- The elevation RTTs clear to `mfSeaFloorElevation` and MAX-blend per pixel; this guard does not change that —
  it only verifies the device can do the blend it now relies on.
- **Heavier alternatives, only if the assert ever fires on a real device** (do not pre-implement — YAGNI):
  (a) switch `keElevationFormat` to a guaranteed-blendable format (e.g. an R32_SFLOAT or other format that
  advertises the bit on the failing device), accepting the bandwidth/footprint cost; or (b) replace MAX blending
  with a depth-test-as-max emulation (write elevation through a depth attachment with `VK_COMPARE_OP_GREATER`),
  avoiding color-blend entirely. Both are materially larger changes and unjustified unless the assert fires.

---

## Secondary / optional: hoist per-placement trig out of `GlobalElevation`

This is a *minor, low-priority* micro-optimization surfaced by the same audit, kept here (rather than as its own
plan file) because it is small, speculative, and shares the elevation-MAX context — folding it in avoids a
near-empty standalone plan. **Only worth doing if a profile flags `GlobalNormal` / AI steering as hot.**

### Context

After the elevation-MAX change, `IslandTerrain::GlobalElevation`
(`Engine/Source/Frame/IslandTerrain.cpp:212-273`) no longer early-returns on first containment — it now iterates
**every** placement in the cell taking a running `std::max`. For each placement it recomputes
`std::cos(-rPlacement.fRotation)` and `std::sin(-rPlacement.fRotation)` (`:245-246`) to inverse-rotate the query
point into island-local space. `GlobalNormal` (`:677-697`) calls `GlobalElevation` four times per query (4-tap
finite difference), so the trig now runs `4 × (placements-per-cell)` times per normal query, with no early exit
to cap it. Cells carry roughly 6-13 placements (`IslandChainPlacement::Generate`).

`IslandPlacement` (`Engine/Source/Frame/IslandChainPlacement.h:8-13`) stores `fRotation` and is generated once
per cell by `IslandChainPlacement::Generate` (single construction site,
`Engine/Source/Frame/IslandChainPlacement.cpp:236`) and deserialized in `FrameStaticData::Read`
(`Engine/Source/Frame/FrameStaticData.cpp:28-33`). The rotation is fixed for the life of the placement.

### Problem

Redundant per-query `cos`/`sin` of a value that is constant per placement, now amortized over every placement on
every `GlobalElevation` call (no early-out). Pure compute waste; not a correctness issue.

### Proposed change (only if profiled)

Cache the inverse-rotation `(cos(-fRotation), sin(-fRotation))` per placement so `GlobalElevation` reads it
instead of recomputing trig each iteration. Two shapes, both must keep the cache **out of the serialized format
and any determinism path** (the cache is derived from `fRotation`, never an independent input):

1. **Compute-once on construction/read**, stored on `IslandPlacement`. **Caveat:** `Engine/Source/Frame/CLAUDE.md`
   states "No transient metadata in Frame structs — only logical state belongs here; derived data goes in lookup
   infrastructure." Adding a derived `(cos, sin)` to `IslandPlacement` (which lives in the serialized
   `FrameStaticData`) bends that rule, and would require populating it at *both* the `Generate` push-back
   (`IslandChainPlacement.cpp:236`) and the `FrameStaticData::Read` deserialize path
   (`FrameStaticData.cpp:28-33`) — `FrameStaticData::Write` must still emit only `islandCrc` / `f2WorldPos` /
   `fRotation`, never the cache.
2. **Side lookup infrastructure** (preferred per the CLAUDE.md convention): a derived per-cell parallel array of
   `(cos, sin)` keyed alongside the placement list, built where the placement list is finalized. Keeps
   `IslandPlacement` purely logical at the cost of a second container to keep in sync.

Determinism note: `cos`/`sin` of the same `fRotation` is bit-identical across client and server (same FMA-disabled
build), so a precomputed cache cannot introduce divergence — but it must still never be serialized or fed into the
shared CRC, exactly because it is derived.

### Critical files (only if pursued)

- `Engine/Source/Frame/IslandTerrain.cpp` — `GlobalElevation` trig (`:245-246`), the no-early-exit max loop
  (`:238-270`); `GlobalNormal` 4× caller (`:677-697`).
- `Engine/Source/Frame/IslandChainPlacement.h` — `IslandPlacement` (`:8-13`), if option 1.
- `Engine/Source/Frame/IslandChainPlacement.cpp` — single placement construction site (`:236`), if option 1.
- `Engine/Source/Frame/FrameStaticData.{h,cpp}` — placement storage and `Read`/`Write` (`.cpp:6-42`); the cache
  must stay out of `Write`.

### Out of scope (for this secondary item)

- The MAX-over-all-placements behavior itself — correct and deterministic; not changed by caching trig.
- Any change to `fRotation`'s meaning, the footprint-rectangle containment test, or the heightmap sampling.
- Serializing or CRC-including the cached values — explicitly forbidden; they are derived.

### Notes

- Do not implement speculatively. The loop is small (single-digit to low-double-digit placements per cell) and
  `GlobalElevation` is not currently known to be hot. This is a "if a profile points here" follow-up only.
