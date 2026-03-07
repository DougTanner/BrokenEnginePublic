# `/Engine/Source/Ui/Screens/` - Engine Debug UI Screens

## Overview

ImGui-based debug overlays rendered by ImGuiManager. Each screen class encapsulates its own state and rendering logic. ImGuiManager owns screen instances and calls their `Render()` methods during the ImGui frame. Game-specific menu screens live in the game project's Ui/Screens folder.

## Key Classes

- **TweaksScreen** - Multi-section parameter adjustment UI providing runtime control over rendering parameters via Wrapper globals. Organized into 14 toggleable sections (`TweakSection` enum) covering PBR, terrain, water, lighting, shadows, smoke, wind, and other visual systems. Split across a subdirectory with core logic in `TweaksScreen.cpp` and one `.cpp` file per section.

## See Also

- [TweaksScreen/CLAUDE.md](TweaksScreen/CLAUDE.md) - TweaksScreen implementation details
- [../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md) - Game-specific menu screens
