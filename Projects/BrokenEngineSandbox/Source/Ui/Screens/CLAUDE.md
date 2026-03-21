# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui-based UI screens for the game, rendered by engine's ImGuiManager. Most screen implementations are client-only (`#ifdef BT_CLIENT`); DeathMenuScreen compiles in both builds.

## Screen Classes

- **TweaksScreen** (`TweaksScreen/`) - Game override of `engine::TweaksScreenBase`. Implements `RenderHexShieldSection()` and `RenderWindDepositsSection()` (pure virtual in base). The constructor inserts game-specific `game::Wrapper` globals into `engine::TweaksSliderMap::Get()`. `Render()` is guarded by `mbShowImGui`. Split into `TweaksScreen.cpp`, `TweaksScreenHexShield.cpp`, and `TweaksScreenWindDeposits.cpp`
- **HudScreen** - In-game overlay showing player shield and armor bars with icons, and a weapon mode toggle (Blasters/Missiles ImGui radio buttons on the left side) that sends `kClientWeaponModeRequest` via `Client`. Has Initialize/Shutdown lifecycle for Vulkan descriptor management
- **MainMenuScreen** - Entry point with Local Server (LAN discovery), Remote Server (placeholder), settings, and quit options plus language selection. Discovery auto-starts when the main menu is shown. The Local Server button shows SCANNING... while discovery is in progress, becomes active when a server is found, and shows CONNECTING... once a connection is in progress. Uses Chinese font globally when Chinese language is selected, with per-button Chinese font for the language selector in other modes
- **ModalScreen** - Centered modal error dialog for connection rejection and desync notifications. Client-only
- **PauseMenuScreen** - In-game pause overlay with resume, settings, main menu, and quit options
- **GraphicsMenuScreen** - Rendering settings panel exposing engine Wrapper variables. Time of day slider appears only in main menu
- **SoundMenuScreen** - Volume sliders for master, music, and sound with a defaults reset option
- **DeathMenuScreen** - Game over screen with respawn button, gated on `GameFlags::kDeathScreen`. Sends respawn request via `Client` on client builds

## MenuUtils

Shared utilities providing RAII menu scaling (`ScopedMenuScale`), UTF-32 to UTF-8 string conversion via workbuffer, and ImGui controls bound to engine Wrapper settings.

## Architecture Notes

Screens check game UI state and game flags to determine visibility, early-returning when not active. All positioning uses proportional screen percentages for resolution independence. ImGuiManager (engine-side) owns instances of these screen classes and calls their `Render()` methods.
