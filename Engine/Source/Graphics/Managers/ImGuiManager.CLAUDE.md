# ImGuiManager

**Global**: `gpImGuiManager`

Integrates Dear ImGui for menu and debug UI rendering with dedicated Vulkan render pass and framebuffers. Renders after main pass with `VK_ATTACHMENT_LOAD_OP_LOAD` to preserve frame content. Dual font support (EFIGS and Chinese). Signals the per-framebuffer fence as the final GPU submission in the frame.

## Screen Delegation

Owns HUD, menu screens (main, pause, graphics, sound, death), and debug screens (tweaks). See [../../Ui/Screens/CLAUDE.md](../../Ui/Screens/CLAUDE.md).
