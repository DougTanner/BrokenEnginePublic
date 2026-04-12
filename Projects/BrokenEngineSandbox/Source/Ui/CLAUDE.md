# `/Projects/BrokenEngineSandbox/Source/Ui/` - Game User Interface

Game-specific user interface implementation providing HUD, menu screens, and localization. Client-only (`#ifdef BT_CLIENT`).

## Overview

The UI system uses ImGui for all game UI rendering. The HUD and menu screens are implemented as ImGui screen classes in the Screens subdirectory, rendered by the engine's ImGuiManager. Screen visibility is driven by `gpGame->meUiState` and game flags, with screens early-returning when not active.

## Key Systems

- **HUD** - In-game overlay showing player shield and armor bars with icons
- **Menu Screens** - Main menu, modal error dialog, pause, graphics settings, sound settings, and death screen
- **Localization** - UTF-32 string table supporting six languages (English, Chinese, Spanish, Portuguese, French, German) with automatic English fallback for missing translations. `InitializeLocalization()` in `Localization.h` sets the locale and uppercases all string table entries at startup (called from `Game` constructor). Menu screens convert localized strings to UTF-8 via workbuffer-based encoding in `MenuUtils.h/.cpp`

## Architecture Notes

ImGuiManager (engine-side) owns instances of all screen classes and calls their `Render()` methods during the ImGui frame. `Wrapper.h` declares 25 game-specific `engine::Wrapper` globals in the `game::` namespace, split into two groups: hex shield parameters (`gHexShield*`, 11 globals) and wind deposit parameters (`gWindDeposit*`, 14 globals). These are consumed by `game::TweaksScreen` and game rendering code. `LightingWrappers.h/.cpp` declares ~50 `game::Wrapper` globals centralizing all effect lighting configuration (point light intensity/area, puff intensity/area, area light intensity/area, etc.) previously scattered as `constexpr` values across collection files. These are consumed by per-keyframe wrapper scaling in controller types and render-time wrapper overrides in `AreaLightsType`.

## See Also

- [Screens/CLAUDE.md](Screens/CLAUDE.md) - ImGui-based UI screens (HUD and menus)
- [Screens/TweaksScreen/](Screens/TweaksScreen/) - `game::TweaksScreen` overriding `engine::TweaksScreenBase` with hex shield and wind deposit sections
