# Clear Empty Textures To Safe Values

## Context

Deferred from the island GPU-lockup fix session. `Texture::Create` (`Engine/Source/Graphics/Objects/Texture.cpp:231-236`), when given a null `rDataFunction`, transitions the image `UNDEFINED → mInfo.eTextureLayout` (e.g. `SHADER_READ_ONLY`) **without clearing it** — the image keeps undefined contents that may then be sampled. The validation-layer message for this (`TransitionUndefinedToReadOnly`) is deliberately suppressed in the `DebugUtilsCallback` (`Engine/Source/Graphics/Managers/InstanceManager.cpp:33-37`), so the suppression now also masks any genuine "sampled before written" regression.

This is purely about undefined **contents**. It is NOT the island eviction use-after-free / invalid-handle lockup (that was a freed-view / generation issue, already fixed) — that bug is out of scope here.

## Design

- For **sampled, non-`kRenderPass` color images** created with a null `rDataFunction`, clear the image to a safe value inside the existing `OneShotCommandBuffer` in the no-data branch of `Texture::Create`: `UNDEFINED → TRANSFER_DST` → `vkCmdClearColorImage` (safe clear value) → final layout (`mInfo.eTextureLayout`). Replaces the current bare `UNDEFINED → final` transition for this class of image.
- This requires `VK_IMAGE_USAGE_TRANSFER_DST_BIT` on the image. Audit which null-`rDataFunction` `Create` call sites are sampled color images and which lack `TRANSFER_DST` — the lighting-blur intermediate/result textures (`Engine/Source/Graphics/Managers/TextureManager.cpp:790`/`:812`) declare `SAMPLED | STORAGE` with no `TRANSFER_DST` and are written by compute before sampling, so either add the bit or exclude them (they are not actually sampled-before-write). Keep the choice minimal: only images that can truly be sampled with undefined contents need the clear.
- Render-pass targets (`kRenderPass`) keep their existing load-op clear path — do not add a redundant `vkCmdClearColorImage` for them.
- Remove the `TransitionUndefinedToReadOnly` suppression block in `DebugUtilsCallback` (`InstanceManager.cpp:33-37`) once the clear lands, so the message can no longer hide a real regression.

## Critical files

- `Engine/Source/Graphics/Objects/Texture.cpp` — the no-data branch of `Texture::Create` (`:231-236`); add the clear-color path here.
- `Engine/Source/Graphics/Objects/Texture.h` — `TextureInfo` / `Create` signature, if a flag or usage assertion is needed to distinguish "clear me" sites.
- `Engine/Source/Graphics/Managers/InstanceManager.cpp` — `DebugUtilsCallback` `TransitionUndefinedToReadOnly` suppression (`:33-37`); remove after the clear lands.
- `Engine/Source/Graphics/Managers/TextureManager.cpp` — null-`rDataFunction` sampled-color `Create` call sites to audit for `TRANSFER_DST` (`:790`/`:812` and any others).

## Out of scope

- Any use-after-free / invalid-handle / freed-view bug — that was the island eviction lockup and is already fixed. This plan addresses undefined *contents* only.
- Depth/stencil images (`vkCmdClearDepthStencilImage` is a separate path) — not addressed.
- Render-pass color/depth targets — they already clear via load-op; do not double-clear.
- Compute storage images that are fully written before any sample (e.g. lighting-blur ping-pong) — they are not sampled with undefined contents; exclude rather than force `TRANSFER_DST` unless the audit shows otherwise.

## Acceptance criteria

- No sampled (non-`kRenderPass`) color image is ever read with undefined contents — every null-`rDataFunction` sampled-color `Create` either clears to a safe value or is shown by the audit to be fully written before first sample.
- The `TransitionUndefinedToReadOnly` suppression is removed from `DebugUtilsCallback`.
- A debug-layer run (`kbVulkanDebugLayers`) is clean — no `TransitionUndefinedToReadOnly` messages, no new validation errors introduced by the added clear.
