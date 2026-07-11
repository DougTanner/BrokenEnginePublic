# ImGuiManager

**Global**: `gpImGuiManager`

Integrates Dear ImGui for menu and debug UI rendering with dedicated Vulkan render pass and framebuffers. Renders after main pass with `VK_ATTACHMENT_LOAD_OP_LOAD` to preserve frame content. Noto Sans SC Light is the single default font for both Latin and CJK text. Signals the per-framebuffer fence as the final GPU submission in the frame.

## Screen Delegation

Owns HUD, menu screens (main, modal, pause, graphics, sound, death), and debug screens (tweaks) as `std::unique_ptr` members — deliberate engine→game ownership, the sanctioned exception noted in the [Engine/Source hub](../../AGENTS.md). Game screen types are forward-declared in `ImGuiManager.h` (avoiding game-layer includes in the header); actual headers are included only in `ImGuiManager.cpp` where `make_unique` is called. See [../../Ui/Screens/AGENTS.md](../../Ui/Screens/AGENTS.md).

## Theming

Style setup splits into geometry and colors. `SetupThemeGeometry(fUiScale)` resets the whole style to ImGui defaults (preserving Colors), re-applies rounding/border/padding, then `ScaleAllSizes(2.0f * fUiScale)` — resetting first neutralizes `ScaleAllSizes`'s cumulative nature, so geometry is safely re-applyable: it runs in the ctor and re-runs from `Prepare()` on a UI-scale change. WindowRounding stays small (8px post-base-scale) because `RegisterOpaqueRect` occlusion rects are rectangular, so with opaque UI larger rounding would occlude the scene behind the rounded-off corners. Colors derive from a per-`engine::UiTheme` palette table and are safely re-applyable: `Prepare()` polls `gUiTheme` and reapplies the full color set on change, and on opacity change rewrites only WindowBg/ChildBg/PopupBg alpha (`gOpaqueUi` forces 1.0, else `gUiOpacity`).

## Agent input

`Prepare` re-issues the agent's synthetic mouse pos (`gpAgentInput->ReissueImGuiMousePos()`) between `ImGui_ImplWin32_NewFrame()` and `ImGui::NewFrame()` when the harness pin is set — the Win32 backend re-queues the physical cursor there, so the re-issue makes the injected pos the frame's last mouse-pos event (last-writer-wins).

Under physical-input suppression (agent-port client — see [Engine/Source hub](../../AGENTS.md)), `Prepare` also neutralizes the Win32 backend's physical cursor and gamepad-nav polls post-NewFrame: with no harness pin set it feeds a no-mouse sentinel instead of the physical cursor pos, and it clears the ImGui gamepad-nav keys.

## UI scaling

`mfUiScale` = framebuffer height / `kfUiReferenceHeight` (2160, the 4K-reference monitor height), recomputed in the ctor and each `Prepare()`. Fonts scale via `style.FontScaleDpi` (composes with the user's Font Size setting); geometry via the `ScaleAllSizes` factor above. Raw UI pixel constants are authored at 4K reference and multiplied by the free `engine::UiScale()` helper at their use sites throughout engine and game UI code — this is the resolution-independence convention for any hard-coded pixel dimension.
