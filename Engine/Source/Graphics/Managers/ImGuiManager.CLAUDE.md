# ImGuiManager

**Global**: `gpImGuiManager`

Integrates Dear ImGui for menu and debug UI rendering with dedicated Vulkan render pass and framebuffers. Renders after main pass with `VK_ATTACHMENT_LOAD_OP_LOAD` to preserve frame content. Dual font support (EFIGS and Chinese). Signals the per-framebuffer fence as the final GPU submission in the frame.

## Screen Delegation

Owns HUD, menu screens (main, modal, pause, graphics, sound, death), and debug screens (tweaks) as `std::unique_ptr` members. `Prepare()` applies `gUiOpacity` to WindowBg/ChildBg/PopupBg alpha each frame; global RGB for these styles is set to (0.1, 0.1, 0.1) at init. Game screen types are forward-declared in `ImGuiManager.h` (avoiding game-layer includes in the header); actual headers are included only in `ImGuiManager.cpp` where `make_unique` is called. See [../../Ui/Screens/CLAUDE.md](../../Ui/Screens/CLAUDE.md).
