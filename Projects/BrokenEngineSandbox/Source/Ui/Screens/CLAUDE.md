# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui-based UI screens for the game, rendered by engine's ImGuiManager. Most screen implementations are client-only (`#ifdef BT_CLIENT`); DeathMenuScreen compiles in both builds.

## Screen Classes

- **HudScreen** - In-game overlay showing player shield and armor bars with icons. Has Initialize/Shutdown lifecycle for Vulkan descriptor management
- **MainMenuScreen** - Entry point with Local Server (LAN discovery), Remote Server (placeholder), settings, and quit options plus language selection. Local Server triggers `Game::StartServerDiscovery()` covering both localhost and LAN
- **ModalScreen** - Centered modal error dialog for connection rejection and desync notifications. Client-only
- **PauseMenuScreen** - In-game pause overlay with resume, settings, main menu, and quit options
- **GraphicsMenuScreen** - Rendering settings panel exposing engine Wrapper variables. Time of day slider appears only in main menu
- **SoundMenuScreen** - Volume sliders for master, music, and sound with a defaults reset option
- **DeathMenuScreen** - Game over screen with respawn button. Sends respawn request via `ClientNetwork` on client builds

## MenuUtils

Shared utilities providing RAII menu scaling (`ScopedMenuScale`), UTF-32 to UTF-8 string conversion via workbuffer, and ImGui controls bound to engine Wrapper settings.

## Architecture Notes

Screens check game UI state and game flags to determine visibility, early-returning when not active. All positioning uses proportional screen percentages for resolution independence. ImGuiManager (engine-side) owns instances of these screen classes and calls their `Render()` methods.
