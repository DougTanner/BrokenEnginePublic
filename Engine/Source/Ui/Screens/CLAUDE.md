# `/Engine/Source/Ui/Screens/` - Engine Debug UI Screens

## Overview

ImGui-based debug overlays rendered by `ImGuiManager`. Each screen class encapsulates its own state and rendering. `ImGuiManager` owns screen instances and calls their `Render()` methods during the ImGui frame. Game-specific menu screens live in the game project's `Ui/Screens`.

## Key Classes

- **TweaksScreenBase** - Multi-section runtime parameter adjustment UI bound to Wrapper globals. Split into per-section files. Exposes pure-virtual and virtual hooks so the game layer can inject game-specific sections and tabs.

## See Also

- [TweaksScreen/CLAUDE.md](TweaksScreen/CLAUDE.md) - TweaksScreenBase architecture
- [../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md) - Game menu screens and `game::TweaksScreen`
