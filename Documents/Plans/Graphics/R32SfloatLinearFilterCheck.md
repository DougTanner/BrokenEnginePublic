# R32_SFLOAT Linear-Filter Capability Check

## Context

The elevation-pipeline refactor switched the heightmap GPU texture from `VK_FORMAT_R16_UNORM` to `VK_FORMAT_R32_SFLOAT` (absolute meters relative to beach). The bindless texture samplers built in `TextureManager::CreateSamplers` use `VK_FILTER_LINEAR` unconditionally (see `mVkSamplerClamp` / `mVkSamplerBorder` / `mVkSamplerRepeat` construction around `TextureManager.cpp:300-365`). Vulkan does NOT mandate that `R32_SFLOAT` support linear filtering — it depends on `VkFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`. Desktop GPUs (NVIDIA / AMD / Intel) advertise this bit in practice, but older mobile / integrated parts may not. `R16_UNORM` had this guarantee; the switch silently dropped it.

On a device without the bit, sampler creation still succeeds — the rendering result is "undefined" per spec. Likely visible as severely aliased / blocky terrain edges in `Terrain.vert` vertex displacement and `Water.frag` heightmap reads, possibly outright corruption with no validation message.

## Design

### Capability query

At `TextureManager::CreateSamplers` entry (before any sampler construction), call `vkGetPhysicalDeviceFormatProperties(gpInstanceManager->mVkPhysicalDevice, VK_FORMAT_R32_SFLOAT, &rFormatProperties)` and test `rFormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT`.

Helper shape (recommended): add `bool common::SupportsLinearFilter(VkFormat format)` (or `graphics::` — match neighbouring free functions) in `Engine/Source/Graphics/GraphicsUtils.{h,cpp}`. Two consumers in the long term — the elevation render-target creation already in `IslandTerrain.cpp` may also want to assert the same capability — so the helper avoids duplicating the query.

### Fallback policy

Recommend **NEAREST fallback with `kWarning` log** so the engine still boots on unsupported hardware:

- On missing bit: store a `bool mbElevationLinearFilterSupported` member (or equivalent static flag) on `TextureManager` set during `CreateSamplers`.
- Build a dedicated `mVkSamplerElevation` sampler with `VK_FILTER_NEAREST` magFilter/minFilter when the bit is missing; otherwise reuse the existing linear `mVkSamplerClamp` shape.
- Route elevation-bindless reads through `mVkSamplerElevation` (vs the general `mVkSamplerClamp`) in the descriptor-write paths that bind the heightmap texture / elevation G-buffer view. Identify those sites during execution — start from the `mVkSamplerClamp` references at `TextureManager.cpp:645,647` and the render-target / island descriptor setup in `IslandTerrain.cpp`.
- `LOG(kGraphics, kWarning, "R32_SFLOAT does not support VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT on this device — falling back to VK_FILTER_NEAREST for elevation sampling. Terrain edges will appear blocky.");`

Reject `LOG(kError) + DEBUG_BREAK` (the alternative): hard-fail at boot for a visual-only degradation is hostile to playtest on unfamiliar hardware. Diagnostic clarity is preserved by the warning + obviously-aliased visual.

### Where the warning fires

`CreateSamplers` runs once at startup inside `TextureManager` construction; the warning will surface in the launch log and is impossible to miss after a device change. No per-frame logging.

## Critical files

- `Engine/Source/Graphics/Managers/TextureManager.cpp` — query + fallback sampler creation in `CreateSamplers`; new `mVkSamplerElevation` (or branch the existing samplers' magFilter/minFilter).
- `Engine/Source/Graphics/Managers/TextureManager.h` — declare the new sampler handle and accessor; possibly a `bool mbElevationLinearFilterSupported`.
- `Engine/Source/Graphics/GraphicsUtils.{h,cpp}` — new `SupportsLinearFilter(VkFormat)` free function wrapping `vkGetPhysicalDeviceFormatProperties`.
- `Engine/Source/Frame/IslandTerrain.cpp` — switch any direct elevation descriptor writes that pick `mVkSamplerClamp` to the elevation-specific sampler accessor.
- No shader changes required — fallback is purely sampler-side.

## Acceptance criteria

- On standard desktop hardware (NVIDIA / AMD / Intel iGPU advertising the bit): no behavior change — elevation samples remain linear-filtered, no warning emitted.
- On hardware lacking the bit (or with the bit masked off for testing — see Verification): startup log contains the `kWarning` line, terrain edges render with visible NEAREST stair-stepping but no validation errors and no crash.
- The query runs exactly once per `Graphics` construction (boot + device-lost recovery), not per frame.
- `vkGetPhysicalDeviceFormatProperties` is called against `gpInstanceManager->mVkPhysicalDevice` — never the logical device.

## Verification

1. **Standard path**: boot the sandbox on the dev machine; confirm no warning fires and terrain looks identical to pre-change.
2. **Forced-fallback path (preferred — does not require alternate hardware)**: temporarily mask the bit by ANDing the query result with `~VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT` behind a `#if 0` / debug toggle in `SupportsLinearFilter`. Boot the sandbox; confirm the warning fires once, terrain renders with visible NEAREST stepping (most obvious at distant beach edges from the default RTS camera height), no validation errors. Revert the toggle.
3. **Validation-layer crosscheck**: with `VK_LAYER_KHRONOS_validation` enabled, run both paths and confirm no `VUID-vkCmdDraw-magFilter-04553` / `VUID-vkCmdDraw-None-02692` warnings in either case (those VUIDs fire when a linear sampler is used against a format lacking the bit).
4. **Integrated GPU (optional)**: if a sufficiently old Intel iGPU is available, run path 1 against it and observe whether the warning surfaces — this is the real-world canary.

## Out of scope

- Similar capability checks for BC4 / BC5 / BC7 compressed formats — assumed universally supported on the target hardware tier and not exercised by this refactor.
- Shader-side bilinear emulation (manual 2x2 tap + lerp in the vertex shader). Not needed at the hardware-supported quality level; the NEAREST fallback is a known-bad visual that signals the user to upgrade or accept the degradation.
- Capability checks for the elevation render-target's `STORAGE_IMAGE` or `COLOR_ATTACHMENT` bits — those land via separate plans if the render-target format set widens further.
- Re-evaluating the `R32_SFLOAT` choice itself. The format pick is settled by the elevation-pipeline plan; this plan only adds defensive capability handling.
- Caching the query result across device-lost recovery — `CreateSamplers` already re-runs and the call is cheap.

## Notes

- The capability bit is a property of the physical device + format pair; it does not vary with image tiling beyond `optimal` vs `linear` tiling (the engine uses `optimal` for the heightmap texture). Only the `optimalTilingFeatures` member needs to be tested.
- If the elevation render-target itself (the `R32_SFLOAT` G-buffer view in `Water.frag`) is sampled with a linear sampler, the same query covers it — both readers go through the same sampler decision.
- Confirm at execution time which of `mVkSamplerClamp` / `mVkSamplerBorder` is actually bound for elevation reads — if both can land on the heightmap descriptor, both need the fallback branch.
