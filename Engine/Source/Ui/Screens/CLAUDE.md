# `/Engine/Source/Ui/Screens/` - Engine Debug UI Screens

## Overview

ImGui-based debug overlays rendered by ImGuiManager. Each screen class encapsulates its own state and rendering logic. ImGuiManager owns screen instances and calls their `Render()` methods during the ImGui frame. Game-specific menu screens live in the game project's Ui/Screens folder.

## Key Classes

- **TweaksScreen** - Multi-section parameter adjustment UI providing runtime control over rendering parameters via Wrapper globals. Organized into 14 toggleable sections (`TweakSection` enum) covering PBR, terrain, water, lighting, shadows, smoke, wind, and other visual systems. Uses a data-driven design with a static slider-to-Wrapper lookup map and function pointer table for section rendering.

## Architecture Notes

**Toggle bar and sections**: A full-width toggle bar at the top of the screen lets users show/hide any combination of the 14 sections. Each section opens as its own auto-resizing ImGui window. Some sections (Pbr, Smoke, Wind Propagation) use 2-column ImGui table layouts for denser parameter display.

**Auto-hide behavior**: When dragging a slider, all other UI elements fade to alpha=0 while preserving layout, so the user can see the visual effect of the parameter change without UI clutter. Window decorations also go transparent for the active section.

**Conditional compilation**: Guarded by `if constexpr (kbEnableDebugInput)` with an early return when the ImGui overlay is hidden.

## See Also

- [../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md](../../../../Projects/BrokenEngineSandbox/Source/Ui/Screens/CLAUDE.md) - Game-specific menu screens
