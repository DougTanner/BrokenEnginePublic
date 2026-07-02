# ImGuiManager

**Global**: `gpImGuiManager`

Integrates Dear ImGui for menu and debug UI rendering with dedicated Vulkan render pass and framebuffers. Renders after main pass with `VK_ATTACHMENT_LOAD_OP_LOAD` to preserve frame content. Dual font support (EFIGS and Chinese). Signals the per-framebuffer fence as the final GPU submission in the frame.

## Screen Delegation

Owns HUD, menu screens (main, modal, pause, graphics, sound, death), and debug screens (tweaks) as `std::unique_ptr` members — deliberate engine→game ownership, the sanctioned exception noted in the [Engine/Source hub](../../CLAUDE.md). Game screen types are forward-declared in `ImGuiManager.h` (avoiding game-layer includes in the header); actual headers are included only in `ImGuiManager.cpp` where `make_unique` is called. See [../../Ui/Screens/CLAUDE.md](../../Ui/Screens/CLAUDE.md).

## Theming

Style setup splits into geometry and colors. Geometry (rounding/border/padding plus the single `ScaleAllSizes(2.0f)`) runs once in the ctor — `ScaleAllSizes` is cumulative, so it must never move into a re-applyable path; WindowRounding stays small (8px post-scale) because `RegisterOpaqueRect` occlusion rects are rectangular, so with opaque UI larger rounding would occlude the scene behind the rounded-off corners. Colors derive from a per-`engine::UiTheme` palette table and are safely re-applyable: `Prepare()` polls `gUiTheme` and reapplies the full color set on change, and on opacity change rewrites only WindowBg/ChildBg/PopupBg alpha (`gOpaqueUi` forces 1.0, else `gUiOpacity`).
