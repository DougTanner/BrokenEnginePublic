# `/Projects/BrokenEngineSandbox/Source/Ui/Screens/` - Game UI Screens

ImGui screens rendered by engine's `ImGuiManager`. Client-only (`#ifdef BT_CLIENT`) except `DeathMenuScreen`, which compiles in both builds (header is pulled into the server vcxproj).

## Shared Patterns

Children do not re-document these:

- **Menu scale**: every screen calls `ImGui::SetWindowFontScale(kfMenuUiScale)` after `Begin()`; `ScopedMenuScale` scales padding/spacing uniformly (see `MenuUtils`).
- **Visibility**: screens check `gpGame->meUiState` and game flags, early-return when inactive.
- **Positioning**: proportional screen percentages for resolution independence.
- **Opaque-UI occlusion**: screens that want depth-pre-pass culling register their rect via `gpImGuiManager->RegisterOpaqueRect()` when `gOpaqueUi` is enabled.
- **Networked controls**: disable via `NetworkUiControl` while awaiting server confirmation.
- **Localization**: UTF-32 table converted to UTF-8 via workbuffer helpers in `MenuUtils`. Chinese font (`gpImGuiManager->mpChineseFont`) is pushed around text when the selected language requires it.
- **Wrapper binding**: settings controls (checkbox, float slider, step buttons) bound to `engine::Wrapper` globals via helpers in `MenuUtils`.

## Screens

- **TweaksScreen** - Game override of `engine::TweaksScreenBase`; implements the hex-shield section and per-explosion-type Particles tabs (3 sub-tabs: Missile / Player / Spaceship), and provides game tabs hosted inside engine sections (Deposits inside Wind; Visible/Lighting inside Lighting Effects). Split into per-tab files. See subdirectory.
- **HudScreen** - In-game overlay: left fleet panel, right focused-player controls. Both panels slide off-screen when the mouse is far away; left-panel mouse proximity also slides the right panel in (one-way coupling). Left panel is force-opened when the camera has no follow target or the focused grid cell has no players.
- **MainMenuScreen** - Local/remote server selection, settings, quit, language. Compile-time `Pch.h` toggles gate auto-launch/auto-connect.
- **ModalScreen** - Centered modal for connection-rejection and desync messages.
- **PauseMenuScreen** - In-game pause overlay.
- **GraphicsMenuScreen** - Rendering settings bound to engine Wrappers.
- **SoundMenuScreen** - Volume sliders with defaults reset.
- **DeathMenuScreen** - Stub; unique in compiling into both builds.

## MenuUtils

Shared helpers: `kfMenuUiScale` constant, `ScopedMenuScale`, UTF-32 to UTF-8 conversion, and ImGui-to-`engine::Wrapper` control bindings.
