# `/Projects/BrokenEngineSandbox/Source/Ui/` - Game User Interface

Game-specific user interface implementation providing HUD, menu screens, and localization. Client-only (`#ifdef BT_CLIENT`).

## Overview

The UI system uses ImGui for all game UI rendering. The HUD and menu screens are implemented as ImGui screen classes in the Screens subdirectory, rendered by the engine's ImGuiManager. Screen visibility is driven by `gpGame->meUiState` and game flags, with screens early-returning when not active.

## Key Systems

- **HUD** - In-game overlay showing player shield and armor bars with icons
- **Menu Screens** - Main menu, pause, graphics settings, sound settings, and death screen
- **Localization** - UTF-32 string table supporting six languages (English, Chinese, Spanish, Portuguese, French, German) with automatic English fallback for missing translations. Menu screens convert localized strings to UTF-8 via workbuffer-based encoding in `MenuUtils.h`

## Architecture Notes

ImGuiManager (engine-side) owns instances of all screen classes and calls their `Render()` methods during the ImGui frame. `Wrapper.h` extends the engine's `WrapperBase` for game-specific runtime-adjustable settings.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based UI screens (HUD and menus)
