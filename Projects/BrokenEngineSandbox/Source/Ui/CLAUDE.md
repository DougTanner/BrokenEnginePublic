# `/Projects/BrokenEngineSandbox/Source/Ui/` - Game User Interface

Game-specific user interface implementation providing HUD and menu screens.

## Overview

The UI system uses ImGui for all game UI rendering. The HUD and menu screens are implemented as ImGui screen classes in the Screens subdirectory, rendered by the engine's ImGuiManager.

## Key Systems

- **HUD** - ImGui-based in-game overlay showing shield/armor bars and secondary weapon (missile) rotary indicator
- **Menu Screens** - ImGui-based menus for main menu, pause, graphics settings, sound settings, and death screen

## Architecture Notes

ImGuiManager owns instances of all screen classes and calls their `Render()` methods during the ImGui frame. Screen visibility is controlled by checking `gpGame->meUiState` and frame flags, with screens early-returning when not active.

## Localization

The `Localization.h` file defines a Language enum and string table supporting English, Chinese, Spanish, Portuguese, French, and German. `TranslatedString()` provides fallback to English for missing translations.

## Files

- **Localization.h** - String table and language selection
- **Wrapper.h** - Game-specific wrapper extensions

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based UI screens (HUD and menus)
