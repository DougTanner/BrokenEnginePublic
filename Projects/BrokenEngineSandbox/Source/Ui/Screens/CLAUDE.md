# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui-based UI screens for the game, rendered by engine's ImGuiManager.

## Overview

Game-specific screens providing HUD, main menu, pause menu, settings, and death screen functionality. All screens use ImGui for rendering and support both keyboard/mouse and gamepad navigation.

## Screen Classes

- **HudScreen** - In-game HUD displaying player shield and armor bars with icons. Bars are centered horizontally near the bottom of the screen in a two-row layout, scaled proportionally to display size. Textures are lazy-loaded via FileManager chunk requests and registered as ImGui descriptors when ready. Uses ImGui background draw list for non-interactive overlay rendering. Visible when `meUiState == kNone` and death screen is not active.
- **MainMenuScreen** - Entry point with Continue/Play/Graphics/Sound/Quit buttons and language selection bar. Switches to Chinese font via `gpImGuiManager->mpChineseFont` when Chinese language is selected. Visible when `meUiState == kPause` and `InMainMenu()` is true.
- **PauseMenuScreen** - In-game pause overlay with Resume/Restart/Graphics/Sound/MainMenu/Quit options. Centered blue panel, visible when `meUiState == kPause` and not in main menu.
- **GraphicsMenuScreen** - Two-column settings panel with fullscreen, presentation mode, multisampling, anisotropy, sample shading, time of day, world detail, and smoke options.
- **SoundMenuScreen** - Volume sliders for master, music, and sound with defaults reset button.
- **DeathMenuScreen** - Game over screen with restart button. Visible when `meUiState == kNone` and `kDeathScreen` frame flag is set.

## MenuUtils.h

Shared utilities for menu screens:
- **kfMenuUiScale** - 2x scale factor for consistent UI sizing
- **ScopedMenuScale** - RAII helper that pushes/pops ImGui style vars for scaled padding and spacing
- **AppendUtf8()** - Writes UTF-32 localized strings as null-terminated UTF-8 into a Workbuffer for ImGui, avoiding heap allocations. Calls `Push()`, encodes UTF-32 code points to UTF-8 bytes via `PushBack<char>()`, appends null terminator, returns `View().data()` as `const char*`, and calls `Pop()`. Used by all menu screens for button labels and text
- **ToUtf8()** - Converts UTF-32 localized strings to a UTF-8 `std::string` (heap-allocating, used where `std::string` is needed)
- **WrapperToggle()/WrapperSlider()** - ImGui controls bound to engine Wrapper settings

## Architecture Notes

Screens check `gpGame->meUiState` and frame flags to determine visibility, early-returning when not active. Positioning uses proportional screen percentages matching the original widget-based layout. ImGuiManager (in engine) owns instances of these game-specific screen classes.
