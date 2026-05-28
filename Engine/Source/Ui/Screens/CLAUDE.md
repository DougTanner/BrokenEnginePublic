# `/Engine/Source/Ui/Screens/` - Engine Debug UI Screens

Hub for engine-scope ImGui debug overlays. Screen classes live in per-screen subdirectories; this directory holds no source itself. Screens are rendered during the ImGui frame driven by `ImGuiManager` (in `Engine/Source/Graphics/Managers/`); the game project's `Ui/Screens` adds game-specific menu and tweaks screens.

## See Also

- [TweaksScreen/CLAUDE.md](TweaksScreen/CLAUDE.md) - `TweaksScreenBase` multi-section runtime parameter UI bound to Wrapper globals
- [../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md) - Game menu screens and `game::TweaksScreen`
