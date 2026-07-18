# Deposit→Spread Barrier Stage / Comment Mismatch

## Context

The lighting deposit→spread barrier in `CommandBufferRecordMain.cpp` (`vkDepositToComputeBarrier`, ~`:77-85`) has `dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT` and a comment reading `// Barrier: deposit MRT color attachment → compute shader reads (spread)`. But `RecordLightingSpreadPipeline` (`:303`) is a **fragment** pass — a chained fullscreen-MRT render (`vkCmdBeginRenderPass` on `mSpreadVkRenderPass` + `mSpreadPipelines[iPass].RecordDraw`, `:342-343`) that reads the deposit textures as sampled images in its fragment shader, not a compute dispatch. So both the `dstStageMask` and the comment name the wrong stage.

Surfaced by the `Architecture_LightingOccupancyRemoval` session-audit. **Pre-existing** — that plan only touched the barrier's `src` side (it removed the occupancy `atomicOr`, so the barrier's `srcStageMask`/`srcAccessMask` are now correctly `COLOR_ATTACHMENT_OUTPUT` / `COLOR_ATTACHMENT_WRITE`). The `dst` mismatch is untouched by that work.

Currently harmless: the spread render pass's own external subpass dependency (in `mSpreadVkRenderPass`) plus the deposit textures' color-attachment→shader-read layout transition provide the actual deposit→spread synchronization, so the misdirected explicit barrier has not manifested a bug. But the barrier is either redundant or wrong-staged, and the comment is actively misleading in a hot render path.

## Design

Verify-then-fix (root cause before editing):

1. **Establish whether the explicit barrier is even needed.** Read `mSpreadVkRenderPass` creation (in `RenderTargetTextures` / the spread render-pass setup) and confirm whether its external subpass dependency + the deposit texture layout transition fully synchronize the deposit color writes to the first spread pass's fragment-shader sampled reads.
   - **If fully covered** → the explicit `vkDepositToComputeBarrier` is redundant; delete it (and its comment), removing a `vkCmdPipelineBarrier` per frame. Confirm with the Vulkan validation layers (no WAR/RAW hazard) and a visual check.
   - **If not fully covered** → correct `dstStageMask` to `VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT` (the real consumer) and drop `VK_ACCESS_SHADER_WRITE_BIT` from `dstAccessMask` (the spread pass only *reads* the deposit output). Reword the comment to "fragment shader reads (spread)".
2. Fix the misleading "compute shader" wording either way.

Also inspect the sibling `vkComputeBarrier` declared at the top of `RecordLightingSpreadPipeline` (~`:307-313`, `SHADER_WRITE`→`SHADER_READ`): confirm it belongs to a later compute phase (combine/temporal) and is correctly staged, and that it is not the intended-but-misplaced deposit→spread barrier. In scope only to rule that out — do not restructure it.

## Critical files

- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — `vkDepositToComputeBarrier` (~`:77-85`), `RecordLightingSpreadPipeline` (`:303`+), sibling `vkComputeBarrier` (~`:307`).
- The spread render-pass creation (`RenderTargetTextures` / `mSpreadVkRenderPass` subpass dependencies) — read-only, to decide whether the explicit barrier is redundant.

## Out of scope

- The inter-spread-pass barriers (`vkSpreadBarrier`, ~`:319-325`) — already correctly stated as color-write → fragment-read.
- Wind/smoke deposit barriers.
- Any lighting-pass restructuring (`WindowedLightingShadowDispatch`, `LightingSpreadEmptySkipCadence`).

## Acceptance criteria

- The deposit→spread barrier's `dstStageMask`/comment match the actual consumer (fragment), OR the barrier is deleted as verified-redundant.
- Vulkan validation layers report no new synchronization hazard; lighting-spread output shows no flicker/artifact.

## Notes

- Invariant exposure: client/graphics-only; no determinism/CRC/wire/`kiVersion`. Runtime GPU-sync change — barrier correctness is **not** compile-checked; validate with the Vulkan validation layers plus a visual check.
- Impact escalates from hygiene to a real (currently-masked) sync-correctness fix if step-1 verification finds the subpass dependency does not cover deposit→spread on all target hardware.
- Shares `CommandBufferRecordMain.cpp` with `WindowedLightingShadowDispatch`, `LightingSpreadEmptySkipCadence`, `DisabledPassGatingPerfAudit`, `ObjectShadowsPassCost` — co-schedule or refresh citations (File Group).
