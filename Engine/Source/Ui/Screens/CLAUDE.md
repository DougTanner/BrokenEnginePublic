# `/Engine/Source/Ui/Screens/` - Engine Debug UI Screens

## Overview

ImGui-based debug overlays rendered by ImGuiManager. Each screen class encapsulates its own state and rendering logic. ImGuiManager owns screen instances and calls their `Render()` methods during the ImGui frame. Game-specific menu screens live in the game project's Ui/Screens folder.

## Key Classes

- **TweaksScreenBase** - Multi-section parameter adjustment UI base class providing runtime control over rendering parameters via Wrapper globals. Organized into 14 toggleable sections (`TweakSection` enum) covering PBR, terrain, water, lighting, shadows, smoke, wind, and other visual systems. Split across a subdirectory with core logic in `TweaksScreenBase.cpp` and one `.cpp` file per section. `RenderHexShieldSection()` and `RenderWindDepositsSection()` are pure virtual; the game-side `game::TweaksScreen` provides these implementations and inserts game-specific Wrapper globals into `TweaksSliderMap` via its constructor.

## See Also

- [TweaksScreen/CLAUDE.md](TweaksScreen/CLAUDE.md) - TweaksScreenBase implementation details
- [../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md) - Game-specific menu screens (including game::TweaksScreen override)
