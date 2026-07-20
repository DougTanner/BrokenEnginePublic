# Restore Main Viewport Presentation

## Context

The exact-source Debug client at baseline `18aa59fa7422a3b16f56894d5f5a1e3c29a550b0` presents an entirely white world viewport while ImGui remains visible. The same controlled-player AgentHarness scenario produced the failure with both the baseline deposit/spread barrier and the corrected fragment-read barrier, so it is pre-existing and independent of that synchronization change.

The failure is localized to the main-scene-to-present path. `CommandBufferRecordMain::Record` records `RecordImageRenderPass` and then `RecordHighDynamicRangeResolve` (`Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp:59-60`); the latter samples `SwapchainManager::mHdrTexture` through `kPipelineHdrResolve` and writes the present framebuffer (`CommandBufferRecordMain.cpp:455`, `PipelineManager.cpp:151`). ImGui subsequently uses `VK_ATTACHMENT_LOAD_OP_LOAD` (`ImGuiManager.cpp:352`), and the captured UI is correct, so the capture and UI overlay preserve a bad scene result rather than creating the white viewport.

Runtime evidence on the exact baseline showed active `VK_LAYER_KHRONOS_validation`, no fallback, no `SYNC-HAZARD`, no device loss, and meaningful finite nonzero Lighting, Spread, and Combine targets. Two `1568x885` screenshots were byte-identical white-world frames (SHA-256 `8924097621379B1C79898D606316A647ABE652A7C10296F2F5A629AD08879295`) despite a live scene with an alive focused player, islands, ships, and projectiles. Restoring only the HDR resolve pipeline's two prior uniform descriptor declarations produced the same screenshot hash, disproving that narrow explanation. An older Debug executable renders the world in the same environment, but its exact source revision is not established; treat it only as behavioral evidence that presentation can work on this machine.

## Design

1. Reproduce the exact controlled-player scenario and localize the first white stage between the F16 HDR scene target and the present image. Add only temporary diagnostics needed to inspect `SwapchainManager::mHdrTexture`; do not retain general-purpose capture machinery unless the final fix requires it.
2. Use current-source stage dumps to determine whether `mHdrTexture` is already white or the white result first appears in `RecordHighDynamicRangeResolve`. Treat the HDR-only uniform-declaration A/B result as disproven and do not repeat it; do not infer a source interval from the older executable without establishing its provenance.
3. Fix the smallest confirmed cause inside the main scene/HDR render-pass, texture setup, resolve-pipeline declaration, or associated shader/packed-data boundary. Preserve the existing F16 HDR intermediate, tone mapping/color grading behavior, ImGui load-preserve pass, and synchronization contracts.
4. Remove temporary diagnostics and verify both the final visible frame and the offscreen targets under active Vulkan validation.

## Critical files

- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — `RecordImageRenderPass` and `RecordHighDynamicRangeResolve`.
- `Engine/Source/Graphics/Managers/SwapchainManager.cpp` / `.h` — `mHdrTexture`, HDR render pass/framebuffer, and present render pass.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `kPipelineHdrResolve` declaration.
- `Engine/Data/Shaders/HdrResolve.frag` — final HDR sample, tone map, and color grade.
- `Engine/Source/Graphics/Managers/ImGuiManager.cpp` — load-preserve overlay boundary; inspect only unless evidence points here.

## Out of scope

- Deposit/spread synchronization; the barrier correction already has independent nonzero-target and validation evidence.
- `Pipeline.cpp` / `PipelineDescriptorWriter.cpp` descriptor-framework changes. If localization reaches those shared paths, stop and coordinate with the live pipeline descriptor/registration plans before broadening scope.
- Redesigning HDR, tone mapping, color grading, screenshot infrastructure, or pipeline descriptor ownership.
- Unrelated rendering quality or performance work.

## Acceptance criteria

- A fresh exact-source Debug client/server run with an alive controlled player shows ocean/terrain/units in two completed-frame screenshots rather than a white world viewport; the scene remains live between captures.
- Lighting, Spread, Combine, and the localized HDR/present stage contain finite meaningful scene data.
- `VK_LAYER_KHRONOS_validation` is active with no fallback, `SYNC-HAZARD`, validation error, device loss, or crash.
- Client builds cleanly; temporary diagnostic code is absent from the final diff.

## Notes

- Client graphics-only runtime fix. No simulation determinism/CRC, `kiVersion`, `.pack` layout, replay, wire protocol, server behavior, or allocation-tracked simulation exposure.
- Shader or packed-data exposure depends on the confirmed root cause. If shader source changes, run the GLSL review and the compile skill's required data/bootstrap path rather than relying on stale Shared shader data.
- Risk tier: Tier 2 scoped rendering behavior. AgentHarness visual verification is required because compile and validation-only checks both pass in the broken state.
