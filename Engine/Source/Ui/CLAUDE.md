# `/Engine/Source/Ui/` - User Interface System

Immediate mode widget-based GUI framework for menus, HUD elements, and in-game overlays.

## Overview

The UI system provides a declarative widget hierarchy for building user interfaces. Widgets are composed using layout containers (VStack/HStack) and primitives (Button, Slider, Toggle, Text). The system handles input routing, focus management, and rendering through the UiManager singleton.

## Key Systems

- **UiManager** - Singleton orchestrating widget updates and rendering. Manages focus state for keyboard/gamepad navigation and mouse capture for drag operations. Global pointer: `gpUiManager`

- **Widget** - Hierarchical UI element supporting layout containers (HStack/VStack), interactive controls (buttons, sliders, toggles, rotaries), and text display. Visibility controlled via lambda-based Enabled callbacks that check game state

- **Wrapper** - Type-safe value container for UI-bound settings. Stores current/previous values with min/max bounds, supports float, bool, and discrete enum types. Change detection via `Changed()` method. Global Wrapper instances in WrapperBase.h expose runtime-adjustable parameters for graphics, audio, and gameplay tuning

## Architecture Notes

Widget visibility uses lambda functions checking game state (`gpGame->meUiState`), allowing the entire widget tree to exist while only rendering relevant screens. Layout uses proportional sizing (0.0-1.0 range) relative to screen dimensions.

Wrapper globals provide runtime adjustment of rendering parameters (exposure, gamma, lighting), audio settings (volumes), and visual tuning (wave counts, shadow feathering). These are accessed by shaders, audio, and rendering code throughout the engine.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based debug screens (TweaksScreen)
