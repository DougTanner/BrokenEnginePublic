# `/Engine/Source/Ui/Screens/` - Engine Debug UI Screens

Hub for engine-scope ImGui debug overlays. This directory holds no source itself; its only screen family is `TweaksScreen/`. Screens are owned and rendered during the ImGui frame by `ImGuiManager` (in `Engine/Source/Graphics/Managers/`); all other screens (menus, HUD) live in the game project's `Ui/Screens`.

## See Also

- [TweaksScreen/CLAUDE.md](TweaksScreen/CLAUDE.md) - `TweaksScreenBase` multi-section runtime parameter UI bound to Wrapper globals
- [../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md) - Game menu screens and `game::TweaksScreen`
