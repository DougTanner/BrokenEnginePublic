<!-- broken-engine-plan/v1 {"createdUtc":"2026-07-20T00:58:38.000Z","dependsOn":[]} -->
# Restore Main Viewport Presentation

## Context

The exact-source Debug client at baseline `18aa59fa7422a3b16f56894d5f5a1e3c29a550b0` presents an entirely white world viewport while ImGui remains visible. The same controlled-player AgentHarness scenario produced the failure with both the baseline deposit/spread barrier and the corrected fragment-read barrier, so it is pre-existing and independent of that synchronization change.

The failure is localized to the main-scene-to-present path. `CommandBufferRecordMain::Record` records `RecordImageRenderPass` then `RecordHighDynamicRangeResolve` (`Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp:59-60`). `RecordHighDynamicRangeResolve` (`CommandBufferRecordMain.cpp:467-476`) draws a fullscreen quad through `kPipelineHdrResolve` — which samples `SwapchainManager::mHdrTexture` as a combined sampler (`PipelineManager.cpp:151-165`) — into the present framebuffer via `gpSwapchainManager->mVkRenderPass`. ImGui then overlays with `VK_ATTACHMENT_LOAD_OP_LOAD` (`ImGuiManager.cpp:352`); the captured UI is correct, so capture and the UI overlay preserve a bad scene result rather than creating the white viewport.

Runtime evidence on the exact baseline: active `VK_LAYER_KHRONOS_validation`, no fallback, no `SYNC-HAZARD`, no device loss, and meaningful finite nonzero Lighting, Spread, and Combine targets. Two `1568x885` screenshots were byte-identical white-world frames (SHA-256 `8924097621379B1C79898D606316A647ABE652A7C10296F2F5A629AD08879295`) despite a live scene with an alive focused player, islands, ships, and projectiles. Restoring only the HDR resolve pipeline's two prior uniform descriptor declarations produced the same screenshot hash, disproving that narrow explanation. An older Debug executable renders the world in the same environment, but its exact source revision is not established; treat it only as behavioral evidence that presentation can work on this machine.

## Design

Diagnostic bugfix executed in four fixed steps. The root cause is not yet known; the fix step is bounded to the localized stage rather than left open-ended.

1. **Reproduce.** Run the controlled-player AgentHarness scenario on current source (Debug client + server, alive controlled player) and confirm the white viewport reproduces with two completed-frame screenshots.
2. **Localize.** Determine whether `SwapchainManager::mHdrTexture` is already white after `RecordImageRenderPass` or the white result first appears in `RecordHighDynamicRangeResolve`. Add only temporary diagnostics needed to dump/inspect `mHdrTexture` and the present image; do not build general-purpose capture machinery. Treat the HDR-only uniform-declaration A/B result as disproven and do not repeat it; do not infer a source interval from the older executable without establishing its provenance.
3. **Fix.** Apply the smallest confirmed-root-cause fix inside the localized stage: the main scene render pass/HDR target setup, the resolve-pipeline declaration, `RecordHighDynamicRangeResolve`, `HdrResolve.frag`, or that shader's packed-data boundary. Preserve the existing F16 HDR intermediate, tone mapping/color grading behavior, the ImGui load-preserve pass, and existing synchronization contracts. Verify root cause before editing (code inspection showing the bug unambiguously, or direct dump/log evidence).
4. **Clean and verify.** Remove all temporary diagnostics, then verify the final visible frame and the offscreen targets under active Vulkan validation per Acceptance criteria.

## Critical files

- `Engine/Source/Graphics/Managers/CommandBufferRecordMain.cpp` — `RecordImageRenderPass` (line 349) and `RecordHighDynamicRangeResolve` (line 467).
- `Engine/Source/Graphics/Managers/SwapchainManager.cpp` / `.h` — `mHdrTexture` (created in `CreateFramebuffers`, line 338; declared `SwapchainManager.h:46`), HDR render pass in `CreateRenderPass` (line 26), and the present render pass `mVkRenderPass`.
- `Engine/Source/Graphics/Managers/PipelineManager.cpp` — `kPipelineHdrResolve` `Create` call (lines 151-165) inside the pipeline-construction sequence.
- `Engine/Data/Shaders/HdrResolve.frag` — final HDR sample, tone map, and color grade.
- `Engine/Source/Graphics/Managers/ImGuiManager.cpp` — load-preserve overlay boundary (`VK_ATTACHMENT_LOAD_OP_LOAD`, line 352); read-only reference unless dump evidence points here.

## Scope contract

The listed scope is both target and ceiling: make the smallest complete change that satisfies the acceptance criteria, and add no abstractions, configuration, refactors, or fixes to adjacent code encountered along the way. Naming a file grants permission only for the named regions plus the mechanical necessities (includes, forward declarations, project membership) the named change requires.

**In scope**

- Temporary, fully removed diagnostics for inspecting `mHdrTexture` and the present image, limited to the Critical-files regions above.
- A confirmed-root-cause fix confined to: `RecordImageRenderPass` / `RecordHighDynamicRangeResolve` in `CommandBufferRecordMain.cpp`; `mHdrTexture`, HDR render pass/framebuffer, or present render pass setup in `SwapchainManager.cpp`/`.h` (`CreateRenderPass`, `CreateFramebuffers`, and the touched member declarations); the `kPipelineHdrResolve` `Create` call in `PipelineManager.cpp`; `HdrResolve.frag` and, only if the shader change requires it, its directly consumed packed-data declarations.
- `ImGuiManager.cpp` edits only if dump evidence localizes the defect to the overlay boundary, and then only at line-352's render-pass attachment setup.

**Out of scope**

- Deposit/spread synchronization; the barrier correction already has independent nonzero-target and validation evidence.
- `Pipeline.cpp` / `PipelineDescriptorWriter.cpp` descriptor-framework changes. If localization reaches those shared paths, stop and coordinate with the live pipeline descriptor/registration plans before broadening scope.
- Redesigning HDR, tone mapping, color grading, screenshot infrastructure, or pipeline descriptor ownership.
- Any other function in the Critical files (e.g. `CreateSwapchain`, `AcquireNextImage`, `Present*`, other pipeline `Create` calls), unrelated rendering quality or performance work, and retained capture machinery.

## Risk tier and invariants

- **Tier 2** — scoped client rendering behavior (single subsystem, no Tier-3 surface). Tier trigger: runtime behavior change in the Graphics subsystem.
- Client graphics-only. Must not touch simulation determinism/CRC, `kiVersion`, `.pack` layout, replay, wire protocol, server behavior, or allocation-tracked simulation code.
- Preserve: F16 HDR intermediate format, tone-map/color-grade output behavior, ImGui `LOAD_OP_LOAD` overlay contract, and existing barrier/synchronization contracts.
- If the fix changes shader source, run the GLSL review and the compile skill's required data/bootstrap path rather than relying on stale shared shader data.

## Acceptance criteria

- A fresh exact-source Debug client/server AgentHarness run with an alive controlled player shows ocean/terrain/units in two completed-frame screenshots rather than a white world viewport; the scene remains live between captures. Harness verification is mandatory because compile and validation-only checks both pass in the broken state.
- Lighting, Spread, Combine, and the localized HDR/present stage contain finite meaningful scene data.
- `VK_LAYER_KHRONOS_validation` is active with no fallback, `SYNC-HAZARD`, validation error, device loss, or crash.
- Client builds cleanly; temporary diagnostic code is absent from the final diff.
